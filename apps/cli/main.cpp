#include <iostream>                                     // ввод/вывод
#include <string>                                       // строки
#include "common/config.h"                              // дефолтный порт
#include "network/tcp_client.h"                         // tcp-клиент
#include "network/request.h"                            // структура запроса
#include "network/response.h"                           // структура ответа
#include "execution/value.h"                            // для печати значений

int main(int argc, char* argv[]) {                      // точка входа cli
    std::string host = "127.0.0.1";                     // адрес по умолчанию
    int port = 7000;                                    // порт по умолчанию
    if (argc > 1) host = argv[1];                       // первый аргумент — хост
    if (argc > 2) port = std::stoi(argv[2]);            // второй — порт

    std::string sql;                                    // текст запроса
    if (argc > 3) {                                     // если sql передан аргументом
        sql = argv[3];                                  // берём его
    } else {                                            // иначе читаем из stdin
        std::cout << "sql> ";                           // приглашение
        std::getline(std::cin, sql);                    // читаем строку
    }

    db::TcpClient client(host, port);                   // создаём клиента
    db::Request req;                                    // формируем запрос
    req.sql = sql;                                      // текст запроса
    req.request_id = "cli";                             // фиктивный идентификатор

    db::Response resp;                                  // сюда придёт ответ
    db::Status status = client.Send(req, &resp);        // отправляем и ждём
    if (!status.ok()) {                                 // сетевая ошибка
        std::cerr << "network error: " << status.message() << "\n";
        return 1;
    }

    if (!resp.ok) {                                     // логическая ошибка на сервере
        std::cerr << "error: " << resp.error << "\n";
        return 1;
    }

    for (const auto& col : resp.result.columns) {       // печатаем заголовки
        std::cout << col << "\t";
    }
    if (!resp.result.columns.empty()) std::cout << "\n"; // перевод строки после заголовков

    for (const auto& row : resp.result.rows) {          // печатаем строки результата
        for (const auto& val : row.values) {            // каждое значение
            switch (val.type()) {                       // по типу
                case db::ValueType::kInt:    std::cout << val.AsInt();    break; // целое
                case db::ValueType::kString: std::cout << val.AsString(); break; // строка
                case db::ValueType::kBool:   std::cout << (val.AsBool() ? "true" : "false"); break; // булево
                case db::ValueType::kNull:   std::cout << "null";         break; // null
            }
            std::cout << "\t";                          // табуляция между колонками
        }
        std::cout << "\n";                              // перевод строки
    }
    std::cout << "affected rows: " << resp.result.affected_rows << "\n"; // итог
    return 0;
}
