#include <iostream>                                     // ввод/вывод
#include <string>                                       // строки
#include "common/config.h"                              // настройки
#include "server/database.h"                            // база данных
#include "server/query_processor.h"                     // исполнитель запросов
#include "server/session_context.h"                     // контекст сессии
#include "execution/value.h"                            // печать значений

int main(int argc, char* argv[]) {                      // точка входа storage_node
    db::Config config;                                  // конфигурация по умолчанию
    if (argc > 1) config.data_dir = argv[1];            // директория данных из аргументов

    db::Database db(config);                            // создаём базу
    db::Status status = db.Start();                     // стартуем
    if (!status.ok()) {                                 // ошибка инициализации
        std::cerr << "failed to start storage node: " << status.message() << "\n";
        return 1;
    }

    db::QueryProcessor qp(&db);                         // процессор запросов

    std::cout << "storage node started. enter sql (empty line to exit):\n"; // приветствие
    std::string line;                                   // буфер строки
    while (std::getline(std::cin, line)) {              // читаем построчно
        if (line.empty()) break;                        // пустая строка — выход

        db::SessionContext ctx;                         // локальный контекст
        ctx.client_id = "local";                        // фиктивный клиент
        db::QueryResult result = qp.Execute(line, &ctx); // выполняем запрос
        if (!result.ok) {                               // ошибка исполнения
            std::cerr << "error: " << result.error << "\n";
            continue;                                   // следующая итерация
        }

        for (const auto& col : result.columns) {        // печатаем заголовки
            std::cout << col << "\t";
        }
        if (!result.columns.empty()) std::cout << "\n";

        for (const auto& row : result.rows) {           // строки
            for (const auto& val : row.values) {        // значения
                switch (val.type()) {
                    case db::ValueType::kInt:    std::cout << val.AsInt();    break;
                    case db::ValueType::kString: std::cout << val.AsString(); break;
                    case db::ValueType::kBool:   std::cout << (val.AsBool() ? "true" : "false"); break;
                    case db::ValueType::kNull:   std::cout << "null";         break;
                }
                std::cout << "\t";
            }
            std::cout << "\n";
        }
        std::cout << "affected rows: " << result.affected_rows << "\n"; // статистика
    }

    db.Stop();                                          // корректная остановка
    return 0;
}
