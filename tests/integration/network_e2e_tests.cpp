#include <gtest/gtest.h>                              // фреймворк тестирования
#include <thread>                                       // для sleep между стартом сервера и коннектом
#include <chrono>                                       // миллисекунды
#include <filesystem>                                 // очистка тестовых директорий
#include "server/database.h"                          // верхнеуровневая база
#include "server/query_processor.h"                   // исполнитель sql
#include "server/storage_service.h"                   // сетевой обработчик
#include "network/tcp_server.h"                       // tcp-сервер
#include "network/tcp_client.h"                       // tcp-клиент

using namespace db;                                   // используем пространство имён проекта

TEST(NetworkE2E, FullPipeline) {                      // полный сквозной сетевой цикл
    Config config;                                    // настройки
    config.data_dir = "./test_data_e2e_net";          // изолированная папка
    config.port = 17778;                              // нестандартный порт чтобы не конфликтовать
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым прогоном

    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем движок

    QueryProcessor qp(&db);                           // процессор запросов
    StorageService service(&qp);                      // сетевой сервис

    TcpServer server(config.host, config.port);       // сервер на локальном адресе
    Status s = server.Start([&service](const Session& session, const Request& req) { // запускаем
        return service.HandleRequest(session, req);     // делегируем storage service
    });
    ASSERT_TRUE(s.ok()) << s.message();               // старт прошёл успешно

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // даём серверу дойти до accept

    TcpClient client(config.host, config.port);       // клиент

    Request req1;                                     // запрос 1: создание базы
    req1.sql = "CREATE DATABASE netdb";
    req1.request_id = "e2e-1";
    Response resp1;
    s = client.Send(req1, &resp1);                    // отправляем и ждём ответ
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp1.ok) << resp1.error;             // создание базы успешно

    Request req2;                                     // запрос 2: use database
    req2.sql = "USE netdb";
    req2.request_id = "e2e-1";
    Response resp2;
    s = client.Send(req2, &resp2);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp2.ok);                            // переключение успешно

    Request req3;                                     // запрос 3: создание таблицы
    req3.sql = "CREATE TABLE items (id INT, label STRING)";
    req3.request_id = "e2e-1";
    Response resp3;
    s = client.Send(req3, &resp3);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp3.ok);                            // таблица создана

    Request req4;                                     // запрос 4: вставка
    req4.sql = "INSERT INTO items (id, label) VALUE (7, \"test\")";
    req4.request_id = "e2e-1";
    Response resp4;
    s = client.Send(req4, &resp4);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp4.ok);                            // вставка успешна
    EXPECT_EQ(resp4.result.affected_rows, 1u);        // затронута одна строка

    Request req5;                                     // запрос 5: выборка
    req5.sql = "SELECT * FROM items";
    req5.request_id = "e2e-1";
    Response resp5;
    s = client.Send(req5, &resp5);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp5.ok);                            // select успешен
    ASSERT_EQ(resp5.result.rows.size(), 1u);          // одна строка в ответе
    EXPECT_EQ(resp5.result.rows[0].values[0].AsInt(), 7);       // проверяем id
    EXPECT_EQ(resp5.result.rows[0].values[1].AsString(), "test"); // проверяем label

    server.Stop();                                    // останавливаем сервер
    db.Stop();                                        // останавливаем движок
}
