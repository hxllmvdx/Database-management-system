// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <iostream> // стандартный ввод-вывод
#include <string> // стандартный строковый тип
#include <thread> // поток для сна в main
#include <chrono> // единицы времени для сна
#include "common/config.h" // конфигурация сервера (код человека 1)
#include "server/database.h" // композиционный объект с engine (код человека 1)
#include "server/query_processor.h" // обработчик sql (код человека 1)
#include "server/storage_service.h" // сетевой entrypoint (код человека 1)
#include "network/tcp_server.h" // tcp-сервер (код человека 1)
#include "network/request.h" // формат входящего запроса (код человека 1)
#include "network/response.h" // формат исходящего ответа (код человека 1)

int main(int argc, char* argv[]) { // точка входа серверного приложения
    db::Config config; // конфиг по умолчанию (host=127.0.0.1, port=7000) (код человека 1)

    // простейший парсинг аргументов командной строки (код человека 1)
    for (int i = 1; i < argc; ++i) { // идём по аргументам (код человека 1)
        std::string arg = argv[i]; // текущий аргумент (код человека 1)
        if (arg == "--port" && i + 1 < argc) config.port = std::stoi(argv[++i]); // парсим порт (код человека 1)
        else if (arg == "--host" && i + 1 < argc) config.host = argv[++i]; // парсим хост (код человека 1)
        else if (arg == "--data_dir" && i + 1 < argc) config.data_dir = argv[++i]; // парсим директорию данных (код человека 1)
    } // for args

    std::cout << "starting server on " << config.host << ":" << config.port << std::endl; // лог запуска (код человека 1)

    db::Database db(config); // создаём объект database (код человека 1)
    auto st = db.Start(); // инициализируем окружение и engine (код человека 1)
    if (!st.ok()) { // не удалось стартовать (код человека 1)
        std::cerr << "failed to start database: " << st.message() << std::endl; // ошибка в stderr (код человека 1)
        return 1; // код ошибки (код человека 1)
    } // if start failed

    db::QueryProcessor qp(&db); // создаём обработчик запросов, привязываем к Database (код человека 1)

    db::StorageService service(&qp); // создаём сетевой сервис, привязываем к QueryProcessor (код человека 1)

    db::TcpServer server(config.host, config.port); // создаём tcp-сервер (код человека 1)
    st = server.Start([&service](const db::Session& session, const db::Request& req) { // запускаем сервер с обработчиком (код человека 1)
        return service.HandleRequest(session, req); // делегируем обработку в StorageService (код человека 1)
    }); // server.Start
    if (!st.ok()) { // не удалось запустить tcp-сервер (код человека 1)
        std::cerr << "failed to start server: " << st.message() << std::endl; // ошибка в stderr (код человека 1)
        db.Stop(); // останавливаем database перед выходом (код человека 1)
        return 1; // код ошибки (код человека 1)
    } // if server start failed

    std::cout << "server is running. send SIGINT or kill to stop." << std::endl; // лог готовности (код человека 1)
    while (true) { // бесконечный цикл удержания процесса (код человека 1)
        std::this_thread::sleep_for(std::chrono::seconds(1)); // спим 1 секунду (код человека 1)
    } // while true
    server.Stop(); // теоретическая точка остановки (код человека 1, недостижима без прерывания)
    db.Stop(); // останавливаем database (код человека 1)
    std::cout << "server stopped." << std::endl; // лог остановки (код человека 1)
    return 0; // успешное завершение (код человека 1)
} // main
