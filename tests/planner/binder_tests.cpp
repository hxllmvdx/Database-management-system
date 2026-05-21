#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include "catalog/catalog_manager.h"
#include "parser/parser.h"
#include "planner/binder.h"

namespace {

std::string TempDir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    return path.string();
}

db::TableDescriptor MakeUsersTable() {
    db::TableDescriptor table;
    table.database_name = "app";
    table.table_name = "users";
    table.schema.columns.push_back({"id", db::ColumnType::kInt, true, true, std::nullopt});
    table.schema.columns.push_back({"name", db::ColumnType::kString, false, false, std::nullopt});
    return table;
}

db::Status BindSql(db::Binder* binder, const std::string& sql) {
    db::Parser parser;
    auto stmt = parser.Parse(sql);
    return binder->Bind("app", *stmt, nullptr);
}

class BinderTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = TempDir("coursedb_binder_tests");
        catalog = std::make_unique<db::CatalogManager>(root);
        ASSERT_TRUE(catalog->CreateDatabase("app").ok());
        ASSERT_TRUE(catalog->CreateTable(MakeUsersTable()).ok());
        binder = std::make_unique<db::Binder>(catalog.get());
    }

    std::string root;
    std::unique_ptr<db::CatalogManager> catalog;
    std::unique_ptr<db::Binder> binder;
};

}  // namespace

TEST_F(BinderTest, RejectsUnknownTableAndUnknownColumns) {
    db::Status status = BindSql(binder.get(), "SELECT * FROM missing WHERE id == 1;");
    EXPECT_FALSE(status.ok());

    status = BindSql(binder.get(), "SELECT (missing) FROM users WHERE id == 1;");
    EXPECT_FALSE(status.ok());

    status = BindSql(binder.get(), "SELECT * FROM users WHERE missing == 1;");
    EXPECT_FALSE(status.ok());
}

TEST_F(BinderTest, RejectsInsertWrongTypesAndMissingRequiredColumn) {
    db::Status status = BindSql(binder.get(), "INSERT INTO users (id, name) VALUE (\"bad\", \"Ann\");");
    EXPECT_FALSE(status.ok());

    status = BindSql(binder.get(), "INSERT INTO users (name) VALUE (\"Ann\");");
    EXPECT_FALSE(status.ok());

    status = BindSql(binder.get(), "INSERT INTO users (id, name) VALUE (1);");
    EXPECT_FALSE(status.ok());
}

TEST_F(BinderTest, RejectsUpdateUnknownColumnAndWrongType) {
    db::Status status = BindSql(binder.get(), "UPDATE users SET missing = 1 WHERE id == 1;");
    EXPECT_FALSE(status.ok());

    status = BindSql(binder.get(), "UPDATE users SET id = \"bad\" WHERE id == 1;");
    EXPECT_FALSE(status.ok());
}

TEST_F(BinderTest, AcceptsValidSelectInsertAndUpdate) {
    EXPECT_TRUE(BindSql(binder.get(), "SELECT * FROM users WHERE id == 1;").ok());
    EXPECT_TRUE(BindSql(binder.get(), "INSERT INTO users (id, name) VALUE (1, \"Ann\");").ok());
    EXPECT_TRUE(BindSql(binder.get(), "UPDATE users SET name = \"Bob\" WHERE id == 1;").ok());
}
