#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "common/config.h"
#include "server/database.h"
#include "server/query_processor.h"
#include "server/storage_service.h"
#include "network/tcp_server.h"
#include "network/request.h"
#include "network/response.h"

int main(int argc, char* argv[]) {
    db::Config config; // конфиг по умолчанию (host=127.0.0.1, port=7000)

    // простейший парсинг аргументов: --port 8080 --host 0.0.0.0 --data_dir ./data
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) config.port = std::stoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) config.host = argv[++i];
        else if (arg == "--data_dir" && i + 1 < argc) config.data_dir = argv[++i];
    }

    std::cout << "starting server on " << config.host << ":" << config.port << std::endl;

    // инициализируем database (хранилище)
    db::Database db(config);
    auto st = db.Start();
    if (!st.ok()) {
        std::cerr << "failed to start database: " << st.message() << std::endl;
        return 1;
    }

    // query processor — обработчик sql
    db::QueryProcessor qp(&db);

    // storage service — entrypoint для сетевых запросов
    db::StorageService service(&qp);

    // tcp сервер
    db::TcpServer server(config.host, config.port);
    st = server.Start([&service](const db::Session& session, const db::Request& req) {
        return service.HandleRequest(session, req); // обрабатываем каждый запрос
    });
    if (!st.ok()) {
        std::cerr << "failed to start server: " << st.message() << std::endl;
        db.Stop();
        return 1;
    }

    std::cout << "server is running. send SIGINT or kill to stop." << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // спим вечно
    }
    server.Stop();
    db.Stop();
    std::cout << "server stopped." << std::endl;
    return 0;
}
