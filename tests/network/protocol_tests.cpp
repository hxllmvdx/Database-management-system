#include <gtest/gtest.h>
#include "network/protocol.h"
#include "network/request.h"
#include "network/response.h"
#include "server/query_result.h"
#include "execution/value.h"

namespace db {

// проверяем round-trip для пустого request
TEST(ProtocolTest, RequestRoundTripEmpty) {
    Request orig{};
    orig.request_id = "";
    orig.database = "";
    orig.sql = "";
    orig.auth_token = "";

    std::string wire = Protocol::SerializeRequest(orig);
    Request out{};
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out));
    EXPECT_EQ(out.request_id, orig.request_id);
    EXPECT_EQ(out.database, orig.database);
    EXPECT_EQ(out.sql, orig.sql);
    EXPECT_EQ(out.auth_token, orig.auth_token);
}

// проверяем round-trip для request с данными
TEST(ProtocolTest, RequestRoundTripWithData) {
    Request orig{};
    orig.request_id = "req-123";
    orig.database = "my_db";
    orig.sql = "SELECT * FROM t;";
    orig.auth_token = "tok_abc";

    std::string wire = Protocol::SerializeRequest(orig);
    Request out{};
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out));
    EXPECT_EQ(out.request_id, orig.request_id);
    EXPECT_EQ(out.database, orig.database);
    EXPECT_EQ(out.sql, orig.sql);
    EXPECT_EQ(out.auth_token, orig.auth_token);
}

// проверяем экранирование спецсимволов в request
TEST(ProtocolTest, RequestEscapesQuotesAndNewlines) {
    Request orig{};
    orig.sql = "line1\nline2\"quoted\"";

    std::string wire = Protocol::SerializeRequest(orig);
    Request out{};
    EXPECT_TRUE(Protocol::DeserializeRequest(wire, &out));
    EXPECT_EQ(out.sql, orig.sql);
}

// проверяем round-trip для пустого response
TEST(ProtocolTest, ResponseRoundTripEmpty) {
    Response orig{};
    orig.ok = true;
    orig.error = "";
    orig.result.affected_rows = 0;

    std::string wire = Protocol::SerializeResponse(orig);
    Response out{};
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out));
    EXPECT_EQ(out.ok, orig.ok);
    EXPECT_EQ(out.error, orig.error);
    EXPECT_EQ(out.result.columns.size(), 0u);
    EXPECT_EQ(out.result.rows.size(), 0u);
    EXPECT_EQ(out.result.affected_rows, orig.result.affected_rows);
}

// проверяем round-trip для response с ошибкой
TEST(ProtocolTest, ResponseRoundTripError) {
    Response orig{};
    orig.ok = false;
    orig.error = "table not found";
    orig.result.affected_rows = 0;

    std::string wire = Protocol::SerializeResponse(orig);
    Response out{};
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out));
    EXPECT_EQ(out.ok, false);
    EXPECT_EQ(out.error, "table not found");
}

// проверяем round-trip для response с данными
TEST(ProtocolTest, ResponseRoundTripWithRows) {
    Response orig{};
    orig.ok = true;
    orig.result.columns = {"id", "name", "flag"};
    Tuple t1;
    t1.values = {Value::Int(42), Value::String("hello"), Value::Bool(true)};
    Tuple t2;
    t2.values = {Value::Int(-1), Value::Null(), Value::Bool(false)};
    orig.result.rows = {t1, t2};
    orig.result.affected_rows = 2;

    std::string wire = Protocol::SerializeResponse(orig);
    Response out{};
    EXPECT_TRUE(Protocol::DeserializeResponse(wire, &out));
    EXPECT_EQ(out.ok, true);
    EXPECT_EQ(out.result.columns.size(), 3u);
    EXPECT_EQ(out.result.rows.size(), 2u);
    EXPECT_EQ(out.result.rows[0].values[0].AsInt(), 42);
    EXPECT_EQ(out.result.rows[0].values[1].AsString(), "hello");
    EXPECT_EQ(out.result.rows[0].values[2].AsBool(), true);
    EXPECT_TRUE(out.result.rows[1].values[1].is_null());
    EXPECT_EQ(out.result.affected_rows, 2u);
}

// проверяем, что десериализация отвергает короткие данные
TEST(ProtocolTest, DeserializeRejectsTruncated) {
    std::string bad = "abc"; // меньше 4 байт
    Request req{};
    EXPECT_FALSE(Protocol::DeserializeRequest(bad, &req));
    Response resp{};
    EXPECT_FALSE(Protocol::DeserializeResponse(bad, &resp));
}

} // namespace db
