#include <gtest/gtest.h>
#include "server/database.h"
#include "server/query_processor.h"
#include "server/session_context.h"
#include "common/config.h"
#include <filesystem>

namespace db {

class QueryProcessorIntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;
    std::unique_ptr<Database> db_;
    std::unique_ptr<QueryProcessor> qp_;
    SessionContext session_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "coursedb_qp_test";
        std::filesystem::remove_all(temp_dir_);

        Config cfg;
        cfg.data_dir = temp_dir_.string();
        db_ = std::make_unique<Database>(cfg);
        ASSERT_TRUE(db_->Start().ok());
        qp_ = std::make_unique<QueryProcessor>(db_.get());
        session_.client_id = "test_client";
    }

    void TearDown() override {
        qp_.reset();
        if (db_) db_->Stop();
        db_.reset();
        std::filesystem::remove_all(temp_dir_);
    }
};

// end-to-end: создание бд, таблицы, вставка, выборка
TEST_F(QueryProcessorIntegrationTest, CreateInsertSelect) {
    auto r = qp_->Execute("CREATE DATABASE testdb;", &session_);
    EXPECT_TRUE(r.ok) << r.error;

    r = qp_->Execute("USE testdb;", &session_);
    EXPECT_TRUE(r.ok);

    r = qp_->Execute("CREATE TABLE users (id int, name string);", &session_);
    EXPECT_TRUE(r.ok) << r.error;

    r = qp_->Execute("INSERT INTO users (id, name) VALUES (1, \"alice\");", &session_);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.affected_rows, 1u);

    r = qp_->Execute("INSERT INTO users (id, name) VALUES (2, \"bob\");", &session_);
    EXPECT_TRUE(r.ok) << r.error;

    r = qp_->Execute("SELECT * FROM users;", &session_);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.rows.size(), 2u);
    EXPECT_EQ(r.columns.size(), 2u);
}

// удаление данных
TEST_F(QueryProcessorIntegrationTest, DeleteAllRows) {
    qp_->Execute("CREATE DATABASE testdb;", &session_);
    qp_->Execute("USE testdb;", &session_);
    qp_->Execute("CREATE TABLE items (id int);", &session_);
    qp_->Execute("INSERT INTO items (id) VALUES (10);", &session_);
    qp_->Execute("INSERT INTO items (id) VALUES (20);", &session_);

    auto r = qp_->Execute("DELETE FROM items;", &session_);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.affected_rows, 2u);

    r = qp_->Execute("SELECT * FROM items;", &session_);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.rows.size(), 0u);
}

// перезапуск и персистентность
TEST_F(QueryProcessorIntegrationTest, RestartPreservesData) {
    qp_->Execute("CREATE DATABASE testdb;", &session_);
    qp_->Execute("USE testdb;", &session_);
    qp_->Execute("CREATE TABLE persist (id int, val string);", &session_);
    qp_->Execute("INSERT INTO persist (id, val) VALUES (42, \"hello\");", &session_);

    // перезапускаем database
    qp_.reset();
    db_->Stop();
    db_.reset();

    Config cfg;
    cfg.data_dir = temp_dir_.string();
    db_ = std::make_unique<Database>(cfg);
    ASSERT_TRUE(db_->Start().ok());
    qp_ = std::make_unique<QueryProcessor>(db_.get());
    session_.current_db.clear(); // сбрасываем контекст

    qp_->Execute("USE testdb;", &session_);
    auto r = qp_->Execute("SELECT * FROM persist;", &session_);
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.rows.size(), 1u);
    ASSERT_EQ(r.columns.size(), 2u);
    // отладка: проверим порядок колонок и типы
    EXPECT_EQ(r.columns[0], "id");
    EXPECT_EQ(r.columns[1], "val");
    EXPECT_EQ(r.rows[0].values[0].type(), db::ValueType::kInt);
    EXPECT_EQ(r.rows[0].values[1].type(), db::ValueType::kString);
    EXPECT_EQ(r.rows[0].values[0].AsInt(), 42);
    EXPECT_EQ(r.rows[0].values[1].AsString(), "hello");
}

} // namespace db
