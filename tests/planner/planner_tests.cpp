#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include "catalog/catalog_manager.h"
#include "parser/parser.h"
#include "planner/planner.h"

namespace {

std::string TempDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    return path.string();
}

db::IndexDescriptor MakeIndex(const std::string& root) {
    db::IndexDescriptor index;
    index.name = "users_id_idx";
    index.table_name = "users";
    index.column_name = "id";
    index.file_path = root + "/users_id_idx.dat";
    index.unique = true;
    return index;
}

}

TEST(PlannerTests, ChoosesIndexScanForIndexedEquality) {
    const std::string root = TempDir("coursedb_planner_index_test");
    db::CatalogManager catalog(root);
    ASSERT_TRUE(catalog.CreateDatabase("app").ok());

    db::TableDescriptor table;
    table.database_name = "app";
    table.table_name = "users";
    table.schema.columns.push_back({"id", db::ColumnType::kInt, true, true, std::nullopt});
    table.schema.columns.push_back({"name", db::ColumnType::kString, false, false, std::nullopt});
    table.indexes.push_back(MakeIndex(root));
    ASSERT_TRUE(catalog.CreateTable(table).ok());

    db::Parser parser;
    auto stmt = parser.Parse("SELECT * FROM users WHERE id == 1;");
    db::Planner planner(&catalog);
    std::unique_ptr<db::PhysicalPlan> plan;
    const db::Status status = planner.BuildPhysicalPlan("app", *stmt, &plan);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->type(), db::PhysicalPlanType::kIndexScan);
}

TEST(PlannerTests, ChoosesSeqScanForNonIndexedPredicate) {
    const std::string root = TempDir("coursedb_planner_seq_test");
    db::CatalogManager catalog(root);
    ASSERT_TRUE(catalog.CreateDatabase("app").ok());

    db::TableDescriptor table;
    table.database_name = "app";
    table.table_name = "users";
    table.schema.columns.push_back({"id", db::ColumnType::kInt, true, true, std::nullopt});
    table.schema.columns.push_back({"name", db::ColumnType::kString, false, false, std::nullopt});
    table.indexes.push_back(MakeIndex(root));
    ASSERT_TRUE(catalog.CreateTable(table).ok());

    db::Parser parser;
    auto stmt = parser.Parse("SELECT * FROM users WHERE name == \"Ann\";");
    db::Planner planner(&catalog);
    std::unique_ptr<db::PhysicalPlan> plan;
    const db::Status status = planner.BuildPhysicalPlan("app", *stmt, &plan);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(plan->type(), db::PhysicalPlanType::kSeqScan);
}
