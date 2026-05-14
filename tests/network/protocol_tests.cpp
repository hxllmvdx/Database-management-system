// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <gtest/gtest.h> // фреймворк для тестирования (код человека 1)
#include "network/protocol.h" // тестируемый класс сериализации (код человека 1)
#include "network/request.h" // формат запроса (код человека 1)
#include "network/response.h" // формат ответа (код человека 1)
#include "server/query_result.h" // формат результата (код человека 1)
#include "common/value.h" // типы значений ячеек (код человека 1)

namespace db { // пространство имён базы данных

// тест: round-trip для пустого request (код человека 1)
TEST(ProtocolTest, RequestRoundTripEmpty) { // тест без fixture
    Request orig{}; // создаём пустой запрос (код человека 1)
    orig.request_id = ""; // пустой id (код человека 1)
    orig.database = ""; // пустая бд (код человека 1)
    orig.sql = ""; // пустой sql (код человека 1)
    orig.auth_token = ""; // пустой токен (код человека 1)

    std::string wire = Protocol::SerializeRequest(orig); // сериализуем (код человека 1)
    Request out{}; // буфер для десериализации (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out)); // проверяем успех десериализации (код человека 1)
    EXPECT_EQ(out.request_id, orig.request_id); // проверяем id (код человека 1)
    EXPECT_EQ(out.database, orig.database); // проверяем бд (код человека 1)
    EXPECT_EQ(out.sql, orig.sql); // проверяем sql (код человека 1)
    EXPECT_EQ(out.auth_token, orig.auth_token); // проверяем токен (код человека 1)
} // RequestRoundTripEmpty

// тест: round-trip для request с данными (код человека 1)
TEST(ProtocolTest, RequestRoundTripWithData) { // тест без fixture
    Request orig{}; // создаём запрос (код человека 1)
    orig.request_id = "req-123"; // конкретный id (код человека 1)
    orig.database = "my_db"; // конкретная бд (код человека 1)
    orig.sql = "SELECT * FROM t;"; // конкретный sql (код человека 1)
    orig.auth_token = "tok_abc"; // конкретный токен (код человека 1)

    std::string wire = Protocol::SerializeRequest(orig); // сериализуем (код человека 1)
    Request out{}; // буфер (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out)); // десериализуем (код человека 1)
    EXPECT_EQ(out.request_id, orig.request_id); // id совпал (код человека 1)
    EXPECT_EQ(out.database, orig.database); // бд совпала (код человека 1)
    EXPECT_EQ(out.sql, orig.sql); // sql совпал (код человека 1)
    EXPECT_EQ(out.auth_token, orig.auth_token); // токен совпал (код человека 1)
} // RequestRoundTripWithData

// тест: экранирование спецсимволов в request (код человека 1)
TEST(ProtocolTest, RequestEscapesQuotesAndNewlines) { // тест без fixture
    Request orig{}; // создаём запрос (код человека 1)
    orig.sql = "line1\nline2\"quoted\""; // sql с переводом строки и кавычками (код человека 1)

    std::string wire = Protocol::SerializeRequest(orig); // сериализуем (код человека 1)
    Request out{}; // буфер (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out)); // десериализуем (код человека 1)
    EXPECT_EQ(out.sql, orig.sql); // проверяем что спецсимволы сохранились (код человека 1)
} // RequestEscapesQuotesAndNewlines

// тест: round-trip для пустого response (код человека 1)
TEST(ProtocolTest, ResponseRoundTripEmpty) { // тест без fixture
    Response orig{}; // создаём пустой ответ (код человека 1)
    orig.ok = true; // успешно (код человека 1)
    orig.error = ""; // пустая ошибка (код человека 1)
    orig.result.affected_rows = 0; // нет затронутых строк (код человека 1)

    std::string wire = Protocol::SerializeResponse(orig); // сериализуем (код человека 1)
    Response out{}; // буфер (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out)); // десериализуем (код человека 1)
    EXPECT_EQ(out.ok, orig.ok); // флаг совпал (код человека 1)
    EXPECT_EQ(out.error, orig.error); // ошибка совпала (код человека 1)
    EXPECT_EQ(out.result.columns.size(), 0u); // колонок нет (код человека 1)
    EXPECT_EQ(out.result.rows.size(), 0u); // строк нет (код человека 1)
    EXPECT_EQ(out.result.affected_rows, orig.result.affected_rows); // affected совпал (код человека 1)
} // ResponseRoundTripEmpty

// тест: round-trip для response с ошибкой (код человека 1)
TEST(ProtocolTest, ResponseRoundTripError) { // тест без fixture
    Response orig{}; // создаём ответ (код человека 1)
    orig.ok = false; // ошибка (код человека 1)
    orig.error = "table not found"; // текст ошибки (код человека 1)
    orig.result.affected_rows = 0; // нет затронутых строк (код человека 1)

    std::string wire = Protocol::SerializeResponse(orig); // сериализуем (код человека 1)
    Response out{}; // буфер (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out)); // десериализуем (код человека 1)
    EXPECT_EQ(out.ok, false); // флаг ошибки сохранился (код человека 1)
    EXPECT_EQ(out.error, "table not found"); // текст ошибки сохранился (код человека 1)
} // ResponseRoundTripError

// тест: round-trip для response с данными (код человека 1)
TEST(ProtocolTest, ResponseRoundTripWithRows) { // тест без fixture
    Response orig{}; // создаём ответ (код человека 1)
    orig.ok = true; // успешно (код человека 1)
    orig.result.columns = {"id", "name", "flag"}; // три колонки (код человека 1)
    Tuple t1; // первая строка (код человека 1)
    t1.values = {Value::Int(42), Value::String("hello"), Value::Bool(true)}; // значения (код человека 1)
    Tuple t2; // вторая строка (код человека 1)
    t2.values = {Value::Int(-1), Value::Null(), Value::Bool(false)}; // значения с null (код человека 1)
    orig.result.rows = {t1, t2}; // две строки (код человека 1)
    orig.result.affected_rows = 2; // затронуто 2 строки (код человека 1)

    std::string wire = Protocol::SerializeResponse(orig); // сериализуем (код человека 1)
    Response out{}; // буфер (код человека 1)
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out)); // десериализуем (код человека 1)
    EXPECT_EQ(out.ok, true); // флаг успеха (код человека 1)
    EXPECT_EQ(out.result.columns.size(), 3u); // три колонки (код человека 1)
    EXPECT_EQ(out.result.rows.size(), 2u); // две строки (код человека 1)
    EXPECT_EQ(out.result.rows[0].values[0].AsInt(), 42); // первое значение первой строки (код человека 1)
    EXPECT_EQ(out.result.rows[0].values[1].AsString(), "hello"); // второе значение (код человека 1)
    EXPECT_EQ(out.result.rows[0].values[2].AsBool(), true); // третье значение (код человека 1)
    EXPECT_TRUE(out.result.rows[1].values[1].is_null()); // null на месте (код человека 1)
    EXPECT_EQ(out.result.affected_rows, 2u); // affected_rows совпал (код человека 1)
} // ResponseRoundTripWithRows

// тест: десериализация отвергает короткие данные (код человека 1)
TEST(ProtocolTest, DeserializeRejectsTruncated) { // тест без fixture
    std::string bad = "abc"; // меньше 4 байт (код человека 1)
    Request req{}; // буфер (код человека 1)
    EXPECT_FALSE(Protocol::DeserializeRequest(bad, &req)); // должно вернуть false (код человека 1)
    Response resp{}; // буфер (код человека 1)
    EXPECT_FALSE(Protocol::DeserializeResponse(bad, &resp)); // должно вернуть false (код человека 1)
} // DeserializeRejectsTruncated

} // namespace db
