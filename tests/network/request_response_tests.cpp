// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <gtest/gtest.h> // фреймворк для тестирования (код человека 1)
#include "network/tcp_server.h" // тестируемый tcp-сервер (код человека 1)
#include "network/tcp_client.h" // тестируемый tcp-клиент (код человека 1)
#include "network/protocol.h" // сериализация (код человека 1)
#include "network/request.h" // формат запроса (код человека 1)
#include "network/response.h" // формат ответа (код человека 1)
#include "server/query_result.h" // формат результата (код человека 1)
#include "common/value.h" // типы значений (код человека 1)
#include <winsock2.h> // winsock для windows (код человека 1)
#include <ws2tcpip.h> // расширенные функции winsock (код человека 1)
#include <thread> // потоки для ожидания сервера (код человека 1)
#include <chrono> // единицы времени (код человека 1)

namespace db { // пространство имён базы данных

// простой echo-обработчик для сетевых тестов (код человека 1)
static Response EchoHandler(const Session& session, const Request& req) { // статическая функция
    Response resp; // формируем ответ (код человека 1)
    resp.ok = true; // успешно (код человека 1)
    resp.result.columns = {"echo_sql", "client_id"}; // две колонки (код человека 1)
    Tuple t; // одна строка результата (код человека 1)
    t.values = {Value::String(req.sql), Value::String(session.client_id)}; // значения колонок (код человека 1)
    resp.result.rows.push_back(std::move(t)); // добавляем строку (код человека 1)
    return resp; // возвращаем echo-ответ (код человека 1)
} // EchoHandler

// тест: клиент подключается и получает ответ через полный round-trip (код человека 1)
TEST(TcpNetworkTest, ClientServerRoundTrip) { // тест без fixture
    TcpServer server("127.0.0.1", 17171); // создаём сервер на localhost:17171 (код человека 1)
    ASSERT_TRUE(server.Start(EchoHandler).ok()); // запускаем сервер с echo-обработчиком (код человека 1)

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // даём серверу время начать слушать (код человека 1)

    TcpClient client("127.0.0.1", 17171); // создаём клиент (код человека 1)
    Request req{}; // формируем запрос (код человека 1)
    req.sql = "SELECT 1;"; // текст sql (код человека 1)
    Response resp{}; // буфер для ответа (код человека 1)
    Status st = client.Send(req, &resp); // отправляем и ждём ответ (код человека 1)
    ASSERT_TRUE(st.ok()) << st.message(); // проверяем что сетевая отправка успешна (код человека 1)

    EXPECT_TRUE(resp.ok); // ответ должен быть успешным (код человека 1)
    ASSERT_EQ(resp.result.rows.size(), 1u); // должна быть одна строка (код человека 1)
    EXPECT_EQ(resp.result.rows[0].values[0].AsString(), "SELECT 1;"); // проверяем echo sql (код человека 1)
    EXPECT_EQ(resp.result.rows[0].values[1].AsString().substr(0, 7), "client_"); // проверяем что client_id сгенерирован (код человека 1)

    ASSERT_TRUE(server.Stop().ok()); // останавливаем сервер (код человека 1)
} // ClientServerRoundTrip

// тест: несколько последовательных запросов в одном соединении (код человека 1)
TEST(TcpNetworkTest, MultipleSequentialRequests) { // тест без fixture
    TcpServer server("127.0.0.1", 17172); // создаём сервер на другом порту (код человека 1)
    ASSERT_TRUE(server.Start(EchoHandler).ok()); // запускаем (код человека 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ждём старта (код человека 1)

    TcpClient client("127.0.0.1", 17172); // создаём клиент (код человека 1)
    for (int i = 0; i < 3; ++i) { // три запроса подряд (код человека 1)
        Request req{}; // новый запрос (код человека 1)
        req.sql = "query_" + std::to_string(i); // уникальный текст запроса (код человека 1)
        Response resp{}; // буфер для ответа (код человека 1)
        ASSERT_TRUE(client.Send(req, &resp).ok()); // отправляем, проверяем успех (код человека 1)
        ASSERT_EQ(resp.result.rows.size(), 1u); // одна строка в ответе (код человека 1)
        EXPECT_EQ(resp.result.rows[0].values[0].AsString(), req.sql); // проверяем echo (код человека 1)
    } // for

    ASSERT_TRUE(server.Stop().ok()); // останавливаем сервер (код человека 1)
} // MultipleSequentialRequests

} // namespace db
