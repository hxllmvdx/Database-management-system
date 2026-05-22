#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include "common/config.h"
#include "execution/executor.h"
#include "runtime/storage_node_engine.h"

namespace {

std::string TempDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    return path.string();
}

std::unique_ptr<db::Expr> EqId(std::int64_t id) {
    auto expr = std::make_unique<db::BinaryExpr>();
    expr->op = db::BinaryOp::kEq;

    auto column = std::make_unique<db::ColumnRefExpr>();
    column->column_name = "id";
    expr->left = std::move(column);

    auto literal = std::make_unique<db::LiteralExpr>();
    literal->value = db::Value::Int(id);
    expr->right = std::move(literal);
    return expr;
}

db::TableDescriptor MakeUsersTable(const std::string& root) {
    db::TableDescriptor table;
    table.database_name = "app";
    table.table_name = "users";
    table.schema.columns.push_back({"id", db::ColumnType::kInt, true, true, std::nullopt});
    table.schema.columns.push_back({"name", db::ColumnType::kString, false, false, std::nullopt});

    db::IndexDescriptor index;
    index.name = "users_id_idx";
    index.table_name = "users";
    index.column_name = "id";
    index.file_path = root + "/users_id_idx.dat";
    index.unique = true;
    table.indexes.push_back(std::move(index));
    return table;
}

class ExecutorCrudTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = TempDir("coursedb_executor_crud_tests");
        db::Config config;
        config.data_dir = root;
        engine = std::make_unique<db::StorageNodeEngine>(config);
        ASSERT_TRUE(engine->Start().ok());
        ASSERT_TRUE(engine->CreateDatabase("app").ok());
        ASSERT_TRUE(engine->CreateTable(MakeUsersTable(root)).ok());
        ctx.current_db = "app";
        ctx.engine = engine.get();
    }

    void TearDown() override {
        if (engine != nullptr) {
            (void)engine->Stop();
        }
        std::filesystem::remove_all(root);
    }

    db::QueryResult Execute(const db::PhysicalPlan& plan) {
        return executor.Execute(plan, &ctx);
    }

    void InsertUser(std::int64_t id, const std::string& name) {
        db::InsertPhysicalPlan plan;
        plan.database_name = "app";
        plan.table_name = "users";
        plan.columns = {"id", "name"};
        plan.rows = {{db::Value::Int(id), db::Value::String(name)}};
        db::QueryResult result = Execute(plan);
        ASSERT_TRUE(result.ok) << result.error;
        ASSERT_EQ(result.affected_rows, 1U);
    }

    std::string root;
    std::unique_ptr<db::StorageNodeEngine> engine;
    db::ExecutorContext ctx;
    db::Executor executor;
};

}  

TEST_F(ExecutorCrudTest, ExecutesInsertSelectUpdateAndDelete) {
    InsertUser(1, "Ann");

    db::SeqScanPhysicalPlan select;
    select.database_name = "app";
    select.table_name = "users";
    select.select_all = true;
    select.predicate = EqId(1);

    db::QueryResult selected = Execute(select);
    ASSERT_TRUE(selected.ok) << selected.error;
    ASSERT_EQ(selected.rows.size(), 1U);
    EXPECT_EQ(selected.rows[0].values[1].AsString(), "Ann");

    db::UpdatePhysicalPlan update;
    update.database_name = "app";
    update.table_name = "users";
    update.assignments = {{"name", db::Value::String("Bob")}};
    update.predicate = EqId(1);

    db::QueryResult updated = Execute(update);
    ASSERT_TRUE(updated.ok) << updated.error;
    EXPECT_EQ(updated.affected_rows, 1U);

    select.predicate = EqId(1);
    selected = Execute(select);
    ASSERT_TRUE(selected.ok) << selected.error;
    ASSERT_EQ(selected.rows.size(), 1U);
    EXPECT_EQ(selected.rows[0].values[1].AsString(), "Bob");

    db::DeletePhysicalPlan del;
    del.database_name = "app";
    del.table_name = "users";
    del.predicate = EqId(1);

    db::QueryResult deleted = Execute(del);
    ASSERT_TRUE(deleted.ok) << deleted.error;
    EXPECT_EQ(deleted.affected_rows, 1U);

    select.predicate = EqId(1);
    selected = Execute(select);
    ASSERT_TRUE(selected.ok) << selected.error;
    EXPECT_TRUE(selected.rows.empty());
}

TEST_F(ExecutorCrudTest, UsesIndexScanForIndexedEquality) {
    InsertUser(7, "Indexed");

    db::IndexScanPhysicalPlan scan;
    scan.database_name = "app";
    scan.table_name = "users";
    scan.index_name = "users_id_idx";
    scan.select_all = true;
    scan.predicate = EqId(7);

    db::QueryResult result = Execute(scan);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.rows.size(), 1U);
    EXPECT_EQ(result.rows[0].values[0].AsInt(), 7);
    EXPECT_EQ(result.rows[0].values[1].AsString(), "Indexed");
}

TEST_F(ExecutorCrudTest, RejectsConstraintViolationsBeforeWrite) {
    db::InsertPhysicalPlan null_id;
    null_id.database_name = "app";
    null_id.table_name = "users";
    null_id.columns = {"id", "name"};
    null_id.rows = {{db::Value::Null(), db::Value::String("bad")}};

    db::QueryResult result = Execute(null_id);
    EXPECT_FALSE(result.ok);

    db::InsertPhysicalPlan wrong_count;
    wrong_count.database_name = "app";
    wrong_count.table_name = "users";
    wrong_count.columns = {"id", "name"};
    wrong_count.rows = {{db::Value::Int(1)}};

    result = Execute(wrong_count);
    EXPECT_FALSE(result.ok);
}
