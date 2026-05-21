#include <gtest/gtest.h>                              // gtest
#include <thread>                                       // для sleep
#include <chrono>                                       // миллисекунды
#include "network/tcp_server.h"                         // тестируемый сервер
#include "network/tcp_client.h"                         // тестируемый клиент
#include "network/protocol.h"                           // для проверки содержимого

using namespace db;                                   // убираем префикс

TEST(Network, SmokeTest) {                            // базовый сетевой тест
    TcpServer server("127.0.0.1", 17777);             // сервер на свободном порту
    bool handler_called = false;                      // флаг вызова обработчика
    Status start_status = server.Start([&handler_called](const Session& session, const Request& req) { // стартуем
        handler_called = true;                        // отмечаем, что дошли
        Response resp;                                // формируем ответ
        resp.ok = true;                               // успех
        resp.result.ok = true;
        resp.result.affected_rows = 1;
        return resp;                                  // отдаём клиенту
    });
    ASSERT_TRUE(start_status.ok()) << start_status.message(); // старт прошёл

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // даём потоку дойти до accept

    TcpClient client("127.0.0.1", 17777);             // клиент к тому же порту
    Request req;                                      // тестовый запрос
    req.sql = "select 1";                             // произвольный sql
    req.request_id = "test-1";                        // идентификатор
    Response resp;                                    // буфер ответа
    Status send_status = client.Send(req, &resp);     // отправляем и получаем
    ASSERT_TRUE(send_status.ok()) << send_status.message(); // сеть работает
    EXPECT_TRUE(resp.ok);                             // ответ успешен
    EXPECT_EQ(resp.result.affected_rows, 1u);         // содержимое не потерялось
    EXPECT_TRUE(handler_called);                      // бизнес-логика вызвана

    server.Stop();                                    // очищаем ресурсы
}
