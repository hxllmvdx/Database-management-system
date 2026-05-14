#include <gtest/gtest.h>
#include "network/tcp_server.h"
#include "network/tcp_client.h"
#include "network/protocol.h"
#include "network/request.h"
#include "network/response.h"
#include "server/query_result.h"
#include "execution/value.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <chrono>

namespace db {

// простой echo-обработчик для тестов
static Response EchoHandler(const Session& session, const Request& req) {
    Response resp;
    resp.ok = true;
    resp.result.columns = {"echo_sql", "client_id"};
    Tuple t;
    t.values = {Value::String(req.sql), Value::String(session.client_id)};
    resp.result.rows.push_back(std::move(t));
    return resp;
}

// smoke test: клиент подключается и получает ответ
TEST(TcpNetworkTest, ClientServerRoundTrip) {
    TcpServer server("127.0.0.1", 17171);
    ASSERT_TRUE(server.Start(EchoHandler).ok()); // запускаем сервер

    // даём серверу время начать слушать
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TcpClient client("127.0.0.1", 17171);
    Request req{};
    req.sql = "SELECT 1;";
    Response resp{};
    Status st = client.Send(req, &resp); // отправляем запрос
    ASSERT_TRUE(st.ok()) << st.message();

    EXPECT_TRUE(resp.ok);
    ASSERT_EQ(resp.result.rows.size(), 1u);
    EXPECT_EQ(resp.result.rows[0].values[0].AsString(), "SELECT 1;");
    EXPECT_EQ(resp.result.rows[0].values[1].AsString().substr(0, 7), "client_");

    ASSERT_TRUE(server.Stop().ok()); // останавливаем сервер
}

// несколько последовательных запросов в одном соединении
TEST(TcpNetworkTest, MultipleSequentialRequests) {
    TcpServer server("127.0.0.1", 17172);
    ASSERT_TRUE(server.Start(EchoHandler).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TcpClient client("127.0.0.1", 17172);
    for (int i = 0; i < 3; ++i) {
        Request req{};
        req.sql = "query_" + std::to_string(i);
        Response resp{};
        ASSERT_TRUE(client.Send(req, &resp).ok());
        ASSERT_EQ(resp.result.rows.size(), 1u);
        EXPECT_EQ(resp.result.rows[0].values[0].AsString(), req.sql);
    }

    ASSERT_TRUE(server.Stop().ok());
}

} // namespace db
