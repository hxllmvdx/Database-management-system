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
