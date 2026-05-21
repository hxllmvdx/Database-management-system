#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>

#include "parser/parser.h"
#include "parser/parse_error.h"

namespace {

std::int64_t LocalTimestampMs(int year,
                              int month,
                              int day,
                              int hour,
                              int minute,
                              int second,
                              int millis) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm)) * 1000 + millis;
}

}  // namespace

TEST(ParserTests, ParsesCreateTableWithIndexedAndNotNullColumns) {
    db::Parser parser;
    auto stmt = parser.Parse(
        "CREATE TABLE users (id INT INDEXED, name STRING NOT_NULL, city STRING);");

    ASSERT_EQ(stmt->kind(), db::StatementKind::kCreateTable);
    const auto& create = static_cast<const db::CreateTableStatement&>(*stmt);
    EXPECT_EQ(create.table_name, "users");
    ASSERT_EQ(create.schema.columns.size(), 3U);
    EXPECT_TRUE(create.schema.columns[0].indexed);
    EXPECT_TRUE(create.schema.columns[0].not_null);
    EXPECT_EQ(create.schema.columns[1].type, db::ColumnType::kString);
    EXPECT_TRUE(create.schema.columns[1].not_null);
}

TEST(ParserTests, ParsesAndOrPriorityAndParentheses) {
    db::Parser parser;
    auto stmt = parser.Parse(
        "SELECT * FROM users WHERE age == 18 OR (name == \"Ann\" AND id == 1);");

    ASSERT_EQ(stmt->kind(), db::StatementKind::kSelect);
    const auto& select = static_cast<const db::SelectStatement&>(*stmt);
    ASSERT_NE(select.where, nullptr);
    ASSERT_EQ(select.where->kind(), db::ExprKind::kLogical);
    const auto& root = static_cast<const db::LogicalExpr&>(*select.where);
    EXPECT_EQ(root.op, db::LogicalOp::kOr);
    ASSERT_EQ(root.right->kind(), db::ExprKind::kLogical);
    EXPECT_EQ(static_cast<const db::LogicalExpr&>(*root.right).op, db::LogicalOp::kAnd);
}

TEST(ParserTests, ParsesSelectAliasesBetweenLike) {
    db::Parser parser;
    auto stmt = parser.Parse(
        "SELECT (id AS user_id, name) FROM app.users WHERE id BETWEEN 1 AND 10 AND name LIKE \"A.*\";");

    const auto& select = static_cast<const db::SelectStatement&>(*stmt);
    EXPECT_EQ(select.database_name, "app");
    EXPECT_EQ(select.table_name, "users");
    ASSERT_EQ(select.columns.size(), 2U);
    EXPECT_EQ(select.columns[0].alias, "user_id");
    ASSERT_NE(select.where, nullptr);
    EXPECT_EQ(select.where->kind(), db::ExprKind::kLogical);
}

TEST(ParserTests, RejectsMixedCaseKeywords) {
    db::Parser parser;
    EXPECT_THROW(parser.Parse("SeLeCt * FROM users;"), db::ParseError);
}

TEST(ParserTests, ParsesRevertTimestampToEpochMilliseconds) {
    db::Parser parser;
    auto stmt = parser.Parse("REVERT users 1970.01.01-03:00:01.250;");

    ASSERT_EQ(stmt->kind(), db::StatementKind::kRevert);
    const auto& revert = static_cast<const db::RevertStatement&>(*stmt);
    EXPECT_EQ(revert.table_name, "users");
    EXPECT_EQ(revert.timestamp_ms, LocalTimestampMs(1970, 1, 1, 3, 0, 1, 250));
}

TEST(ParserTests, RejectsUpdateAndDeleteWithoutWhere) {
    db::Parser parser;

    EXPECT_THROW(parser.Parse("UPDATE users SET name = \"Ann\";"), db::ParseError);
    EXPECT_THROW(parser.Parse("DELETE FROM users;"), db::ParseError);
}
