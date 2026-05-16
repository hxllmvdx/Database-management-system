#include <gtest/gtest.h>                              // фреймворк gtest
#include "network/protocol.h"                         // тестируемый модуль
#include "execution/value.h"                          // для формирования tuple

using namespace db;                                   // чтобы не писать префикс

TEST(Protocol, RoundTripRequest) {                    // проверяем сериализацию запроса
    Request req;                                      // исходный запрос
    req.request_id = "req-42";                        // идентификатор
    req.database = "test_db";                         // имя базы
    req.sql = "select * from t";                      // текст sql
    req.auth_token = "secret";                        // токен

    std::string data = Protocol::SerializeRequest(req); // упаковываем
    Request out;                                      // куда распакуем
    ASSERT_TRUE(Protocol::DeserializeRequest(data, &out)); // должно распарситься
    EXPECT_EQ(out.request_id, req.request_id);        // поля совпадают
    EXPECT_EQ(out.database, req.database);
    EXPECT_EQ(out.sql, req.sql);
    EXPECT_EQ(out.auth_token, req.auth_token);
}

TEST(Protocol, RoundTripResponse) {                   // проверяем сериализацию ответа
    Response resp;                                    // исходный ответ
    resp.ok = true;                                   // общий успех
    resp.error = "";                                  // без ошибки
    resp.result.ok = true;                            // результат успешен
    resp.result.error = "";
    resp.result.affected_rows = 2;                    // две строки затронуты
    resp.result.columns = {"id", "name"};             // две колонки
    resp.result.rows = {                              // две строки данных
        Tuple{{Value::Int(1), Value::String("alice")}},
        Tuple{{Value::Int(2), Value::String("bob")}}
    };

    std::string data = Protocol::SerializeResponse(resp); // упаковываем
    Response out;                                     // куда распакуем
    ASSERT_TRUE(Protocol::DeserializeResponse(data, &out)); // парсится
    EXPECT_TRUE(out.ok);                              // флаг сохранился
    EXPECT_EQ(out.result.affected_rows, 2u);          // счётчик на месте
    EXPECT_EQ(out.result.columns.size(), 2u);         // колонки на месте
    EXPECT_EQ(out.result.rows.size(), 2u);            // строки на месте
    EXPECT_EQ(out.result.rows[0].values[0].AsInt(), 1); // данные не испортились
    EXPECT_EQ(out.result.rows[0].values[1].AsString(), "alice");
}

TEST(Protocol, CorruptedDataReturnsFalse) {           // проверка защиты от мусора
    std::string garbage = "not-a-valid-payload";      // явно битые данные
    Request req;
    EXPECT_FALSE(Protocol::DeserializeRequest(garbage, &req)); // должно вернуть false
    Response resp;
    EXPECT_FALSE(Protocol::DeserializeResponse(garbage, &resp));
}
