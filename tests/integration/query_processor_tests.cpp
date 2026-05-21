#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "common/config.h"
#include "server/database.h"
#include "server/query_processor.h"
#include "server/session_context.h"

namespace {

std::string FormatRevertTimestamp(std::chrono::system_clock::time_point point) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        point.time_since_epoch()) % 1000;
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(point);
    std::tm local_time{};
    localtime_r(&raw_time, &local_time);

    std::ostringstream out;
    out << std::put_time(&local_time, "%Y.%m.%d-%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return out.str();
}

class QueryProcessorIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.data_dir = "./test_data_query_processor_sql";
        std::filesystem::remove_all(config.data_dir);
        database = std::make_unique<db::Database>(config);
        ASSERT_TRUE(database->Start().ok());
        processor = std::make_unique<db::QueryProcessor>(database.get());
        session.client_id = "client";
    }

    void TearDown() override {
        if (database != nullptr) {
            (void)database->Stop();
        }
        std::filesystem::remove_all(config.data_dir);
    }

    db::QueryResult Exec(const std::string& sql) {
        return processor->Execute(sql, &session);
    }

    db::Config config;
    std::unique_ptr<db::Database> database;
    std::unique_ptr<db::QueryProcessor> processor;
    db::SessionContext session;
};

}  // namespace

TEST_F(QueryProcessorIntegrationTest, RevertSqlRestoresTableState) {
    ASSERT_TRUE(Exec("CREATE DATABASE testdb;").ok);
    ASSERT_TRUE(Exec("USE testdb;").ok);
    ASSERT_TRUE(Exec("CREATE TABLE users (id INT INDEXED, name STRING NOT_NULL);").ok);

    const std::string before_insert =
        FormatRevertTimestamp(std::chrono::system_clock::now());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    db::QueryResult insert = Exec("INSERT INTO users (id, name) VALUE (1, \"alice\");");
    ASSERT_TRUE(insert.ok) << insert.error;
    ASSERT_EQ(insert.affected_rows, 1U);

    db::QueryResult selected = Exec("SELECT * FROM users WHERE id == 1;");
    ASSERT_TRUE(selected.ok) << selected.error;
    ASSERT_EQ(selected.rows.size(), 1U);

    db::QueryResult reverted = Exec("REVERT users " + before_insert + ";");
    ASSERT_TRUE(reverted.ok) << reverted.error;

    db::QueryResult after = Exec("SELECT * FROM users WHERE id == 1;");
    ASSERT_TRUE(after.ok) << after.error;
    EXPECT_TRUE(after.rows.empty());
}

TEST_F(QueryProcessorIntegrationTest, IndexedInsertAndSelectUseStorageIndexMaintenance) {
    ASSERT_TRUE(Exec("CREATE DATABASE testdb;").ok);
    ASSERT_TRUE(Exec("USE testdb;").ok);
    ASSERT_TRUE(Exec("CREATE TABLE users (id INT INDEXED, name STRING);").ok);

    db::QueryResult insert = Exec("INSERT INTO users (id, name) VALUE (42, \"bob\");");
    ASSERT_TRUE(insert.ok) << insert.error;

    db::QueryResult selected = Exec("SELECT * FROM users WHERE id == 42;");
    ASSERT_TRUE(selected.ok) << selected.error;
    ASSERT_EQ(selected.rows.size(), 1U);
    EXPECT_EQ(selected.rows[0].values[0].AsInt(), 42);
    EXPECT_EQ(selected.rows[0].values[1].AsString(), "bob");
}

using namespace db;

TEST(QueryProcessor, CreateTableAndInsert) {
    Config config;
    config.data_dir = "./test_data_qp";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult r1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    EXPECT_TRUE(r1.ok) << r1.error;

    QueryResult r2 = qp.Execute("USE testdb;", &ctx);
    EXPECT_TRUE(r2.ok) << r2.error;

    QueryResult r3 = qp.Execute("CREATE TABLE users (id INT, name STRING);", &ctx);
    EXPECT_TRUE(r3.ok) << r3.error;

    QueryResult r4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\");", &ctx);
    EXPECT_TRUE(r4.ok) << r4.error;
    EXPECT_EQ(r4.affected_rows, 1u);

    QueryResult r5 = qp.Execute("SELECT * FROM users;", &ctx);
    EXPECT_TRUE(r5.ok) << r5.error;
    ASSERT_EQ(r5.rows.size(), 1u);
    EXPECT_EQ(r5.rows[0].values[0].AsInt(), 1);
    EXPECT_EQ(r5.rows[0].values[1].AsString(), "alice");

    db.Stop();
}

TEST(QueryProcessor, SyntaxErrorReturnsFailure) {
    Config config;
    config.data_dir = "./test_data_qp_err";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult r = qp.Execute("INVALID SQL", &ctx);
    EXPECT_FALSE(r.ok);

    db.Stop();
}

TEST(QueryProcessor, InsertManyAndSelectWhere) {
    Config config;
    config.data_dir = "./test_data_qp_many";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb;", &ctx);
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE nums (id INT, val STRING);", &ctx);
    ASSERT_TRUE(rc3.ok);

    for (int i = 1; i <= 100; ++i) {
        std::string sql = "INSERT INTO nums (id, val) VALUE (" +
                          std::to_string(i) + ", \"v" + std::to_string(i) + "\");";
        QueryResult r = qp.Execute(sql, &ctx);
        ASSERT_TRUE(r.ok) << r.error;
    }

    QueryResult r = qp.Execute("SELECT * FROM nums WHERE id > 50;", &ctx);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.rows.size(), 50u);
    EXPECT_EQ(r.rows[0].values[0].AsInt(), 51);

    db.Stop();
}

TEST(QueryProcessor, DeleteThenSelectReturnsEmpty) {
    Config config;
    config.data_dir = "./test_data_qp_del";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb;", &ctx);
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING);", &ctx);
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\");", &ctx);
    ASSERT_TRUE(rc4.ok);
    QueryResult rc5 = qp.Execute("INSERT INTO users (id, name) VALUE (2, \"bob\");", &ctx);
    ASSERT_TRUE(rc5.ok);

    QueryResult del = qp.Execute("DELETE FROM users WHERE id == 1;", &ctx);
    EXPECT_TRUE(del.ok) << del.error;
    EXPECT_EQ(del.affected_rows, 1u);

    QueryResult sel = qp.Execute("SELECT * FROM users;", &ctx);
    EXPECT_TRUE(sel.ok) << sel.error;
    ASSERT_EQ(sel.rows.size(), 1u);
    EXPECT_EQ(sel.rows[0].values[0].AsInt(), 2);
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "bob");

    db.Stop();
}

TEST(QueryProcessor, UpdateThenSelectReturnsNewValue) {
    Config config;
    config.data_dir = "./test_data_qp_upd";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb;", &ctx);
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING);", &ctx);
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\");", &ctx);
    ASSERT_TRUE(rc4.ok);

    QueryResult upd = qp.Execute("UPDATE users SET name = \"charlie\" WHERE id == 1;", &ctx);
    EXPECT_TRUE(upd.ok) << upd.error;
    EXPECT_EQ(upd.affected_rows, 1u);

    QueryResult sel = qp.Execute("SELECT * FROM users;", &ctx);
    EXPECT_TRUE(sel.ok) << sel.error;
    ASSERT_EQ(sel.rows.size(), 1u);
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "charlie");

    db.Stop();
}

TEST(QueryProcessor, MultipleTablesSimultaneously) {
    Config config;
    config.data_dir = "./test_data_qp_multi";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb;", &ctx);
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE t1 (id INT, name STRING);", &ctx);
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("CREATE TABLE t2 (id INT, age INT);", &ctx);
    ASSERT_TRUE(rc4.ok);

    QueryResult rc5 = qp.Execute("INSERT INTO t1 (id, name) VALUE (1, \"alice\");", &ctx);
    ASSERT_TRUE(rc5.ok);
    QueryResult rc6 = qp.Execute("INSERT INTO t2 (id, age) VALUE (1, 25);", &ctx);
    ASSERT_TRUE(rc6.ok);

    QueryResult r1 = qp.Execute("SELECT * FROM t1;", &ctx);
    EXPECT_TRUE(r1.ok) << r1.error;
    ASSERT_EQ(r1.rows.size(), 1u);
    EXPECT_EQ(r1.rows[0].values[1].AsString(), "alice");

    QueryResult r2 = qp.Execute("SELECT * FROM t2;", &ctx);
    EXPECT_TRUE(r2.ok) << r2.error;
    ASSERT_EQ(r2.rows.size(), 1u);
    EXPECT_EQ(r2.rows[0].values[1].AsInt(), 25);

    db.Stop();
}

TEST(QueryProcessor, ErrorDoesNotCorruptDatabaseState) {
    Config config;
    config.data_dir = "./test_data_qp_err_state";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx);
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb;", &ctx);
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING);", &ctx);
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\");", &ctx);
    ASSERT_TRUE(rc4.ok);

    QueryResult bad = qp.Execute("THIS IS NOT SQL", &ctx);
    EXPECT_FALSE(bad.ok);

    QueryResult sel = qp.Execute("SELECT * FROM users;", &ctx);
    EXPECT_TRUE(sel.ok) << sel.error;
    ASSERT_EQ(sel.rows.size(), 1u);
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "alice");

    db.Stop();
}
