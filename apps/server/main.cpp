#include <iostream>                                     // консольный ввод/вывод
#include <string>                                       // строки
#include "common/config.h"                              // конфигурация сервера
#include "server/database.h"                            // верхнеуровневая база
#include "server/query_processor.h"                     // исполнитель sql
#include "server/storage_service.h"                     // сетевой обработчик
#include "network/tcp_server.h"                         // tcp-сервер

int main(int argc, char* argv[]) {                      // точка входа серверного приложения
    db::Config config;                                  // конфиг по умолчанию
    if (argc > 1) config.host = argv[1];                // хост из аргументов
    if (argc > 2) config.port = std::stoi(argv[2]);     // порт из аргументов

    db::Database db(config);                            // создаём базу
    db::Status status = db.Start();                     // стартуем движок и окружение
    if (!status.ok()) {                                 // ошибка запуска
        std::cerr << "failed to start database: " << status.message() << "\n"; // пишем в stderr
        return 1;                                       // не нулевой код возврата
    }

    db::QueryProcessor qp(&db);                         // процессор запросов привязан к базе
    db::StorageService service(&qp);                    // сетевой сервис использует процессор

    db::TcpServer server(config.host, config.port);     // tcp-сервер на заданном адресе
    status = server.Start([&service](const db::Session& session, const db::Request& req) { // лямбда-обработчик
        return service.HandleRequest(session, req);     // делегируем storage service
    });
    if (!status.ok()) {                                 // не удалось занять порт
        std::cerr << "failed to start server: " << status.message() << "\n";
        db.Stop();                                      // гасим базу корректно
        return 1;
    }

    std::cout << "server listening on " << config.host << ":" << config.port << "\n"; // успешный старт
    std::cout << "press enter to stop...\n";            // ждём сигнала оператора
    std::cin.get();                                     // блокируемся до enter

    server.Stop();                                      // останавливаем tcp
    db.Stop();                                          // останавливаем движок
    return 0;                                           // штатное завершение
}
