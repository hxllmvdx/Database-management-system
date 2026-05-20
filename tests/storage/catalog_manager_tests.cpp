#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "catalog/catalog_manager.h"

namespace {

namespace fs = std::filesystem;

class CatalogManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_catalog_manager_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::CatalogManager CreateManager() const {
        return db::CatalogManager(root_dir_.string());
    }

    db::TableDescriptor MakeTable(const std::string& table_name) const {
        db::TableDescriptor desc;
        desc.database_name = "testdb";
        desc.table_name = table_name;
        desc.data_file = (root_dir_ / "testdb" / "tables" / (table_name + ".tbl")).string();
        desc.index_dir = (root_dir_ / "testdb" / "indexes" / table_name).string();

        db::ColumnSchema id_column;
        id_column.name = "id";
        id_column.type = db::ColumnType::kInt;
        id_column.not_null = true;
        id_column.indexed = true;
        id_column.default_value = db::Value::Int(1);

        db::ColumnSchema name_column;
        name_column.name = "name";
        name_column.type = db::ColumnType::kString;
        name_column.default_value = db::Value::String("guest");

        desc.schema.columns = {id_column, name_column};

        db::IndexDescriptor index;
        index.name = table_name + "_id_idx";
        index.table_name = table_name;
        index.column_name = "id";
        index.file_path = (root_dir_ / "testdb" / "indexes" / table_name / (index.name + ".idx")).string();
        index.unique = true;
        desc.indexes.push_back(index);

        return desc;
    }

    static void ExpectDescriptorsEqual(const db::TableDescriptor& actual,
                                       const db::TableDescriptor& expected) {
        EXPECT_EQ(actual.database_name, expected.database_name);
        EXPECT_EQ(actual.table_name, expected.table_name);
        EXPECT_EQ(actual.data_file, expected.data_file);
        EXPECT_EQ(actual.index_dir, expected.index_dir);

        ASSERT_EQ(actual.schema.columns.size(), expected.schema.columns.size());
        for (std::size_t i = 0; i < actual.schema.columns.size(); ++i) {
            const db::ColumnSchema& lhs = actual.schema.columns[i];
            const db::ColumnSchema& rhs = expected.schema.columns[i];
            EXPECT_EQ(lhs.name, rhs.name);
            EXPECT_EQ(lhs.type, rhs.type);
            EXPECT_EQ(lhs.not_null, rhs.not_null);
            EXPECT_EQ(lhs.indexed, rhs.indexed);
            EXPECT_EQ(lhs.default_value.has_value(), rhs.default_value.has_value());
            if (lhs.default_value.has_value()) {
                EXPECT_EQ(*lhs.default_value, *rhs.default_value);
            }
        }

        ASSERT_EQ(actual.indexes.size(), expected.indexes.size());
        for (std::size_t i = 0; i < actual.indexes.size(); ++i) {
            const db::IndexDescriptor& lhs = actual.indexes[i];
            const db::IndexDescriptor& rhs = expected.indexes[i];
            EXPECT_EQ(lhs.name, rhs.name);
            EXPECT_EQ(lhs.table_name, rhs.table_name);
            EXPECT_EQ(lhs.column_name, rhs.column_name);
            EXPECT_EQ(lhs.file_path, rhs.file_path);
            EXPECT_EQ(lhs.unique, rhs.unique);
        }
    }

    fs::path root_dir_;
};

TEST_F(CatalogManagerTest, CreateDatabaseCreatesExpectedDirectoryTree) {
    db::CatalogManager manager = CreateManager();

    db::Status status = manager.CreateDatabase("testdb");

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_TRUE(fs::is_directory(root_dir_ / "testdb"));
    EXPECT_TRUE(fs::is_directory(root_dir_ / "testdb" / "catalog"));
    EXPECT_TRUE(fs::is_directory(root_dir_ / "testdb" / "tables"));
    EXPECT_TRUE(fs::is_directory(root_dir_ / "testdb" / "indexes"));
    EXPECT_TRUE(fs::is_directory(root_dir_ / "testdb" / "versions"));
}

TEST_F(CatalogManagerTest, CreateTableThenGetTablePreservesDescriptor) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());

    const db::TableDescriptor expected = MakeTable("users");
    ASSERT_TRUE(manager.CreateTable(expected).ok());

    db::TableDescriptor actual;
    db::Status status = manager.GetTable("testdb", "users", &actual);

    ASSERT_TRUE(status.ok()) << status.message();
    ExpectDescriptorsEqual(actual, expected);
}

TEST_F(CatalogManagerTest, ListTablesReturnsStoredTablesSortedByName) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());

    ASSERT_TRUE(manager.CreateTable(MakeTable("zeta")).ok());
    ASSERT_TRUE(manager.CreateTable(MakeTable("alpha")).ok());
    ASSERT_TRUE(manager.CreateTable(MakeTable("middle")).ok());

    std::vector<db::TableDescriptor> tables;
    db::Status status = manager.ListTables("testdb", &tables);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_EQ(tables.size(), 3U);
    EXPECT_EQ(tables[0].table_name, "alpha");
    EXPECT_EQ(tables[1].table_name, "middle");
    EXPECT_EQ(tables[2].table_name, "zeta");
}

TEST_F(CatalogManagerTest, DropTableRemovesMetadataAndLookupFails) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());
    ASSERT_TRUE(manager.CreateTable(MakeTable("users")).ok());
    ASSERT_TRUE(fs::exists(root_dir_ / "testdb" / "catalog" / "users.meta"));

    db::Status drop_status = manager.DropTable("testdb", "users");

    ASSERT_TRUE(drop_status.ok()) << drop_status.message();
    EXPECT_FALSE(fs::exists(root_dir_ / "testdb" / "catalog" / "users.meta"));

    db::TableDescriptor desc;
    db::Status get_status = manager.GetTable("testdb", "users", &desc);
    EXPECT_FALSE(get_status.ok());
}

TEST_F(CatalogManagerTest, DropDatabaseRemovesEntireDirectoryTree) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());
    ASSERT_TRUE(manager.CreateTable(MakeTable("users")).ok());

    db::Status status = manager.DropDatabase("testdb");

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_FALSE(fs::exists(root_dir_ / "testdb"));
}

TEST_F(CatalogManagerTest, CreateTableRejectsDuplicateColumns) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());

    db::TableDescriptor desc;
    desc.database_name = "testdb";
    desc.table_name = "broken";
    desc.data_file = (root_dir_ / "testdb" / "tables" / "broken.tbl").string();
    desc.index_dir = (root_dir_ / "testdb" / "indexes" / "broken").string();

    db::ColumnSchema first;
    first.name = "dup";
    first.type = db::ColumnType::kInt;

    db::ColumnSchema second;
    second.name = "dup";
    second.type = db::ColumnType::kString;

    desc.schema.columns = {first, second};

    db::Status status = manager.CreateTable(desc);

    EXPECT_FALSE(status.ok());
}

TEST_F(CatalogManagerTest, GetTableFailsForCorruptedMetadata) {
    db::CatalogManager manager = CreateManager();
    ASSERT_TRUE(manager.CreateDatabase("testdb").ok());

    const fs::path meta_path = root_dir_ / "testdb" / "catalog" / "broken.meta";
    std::ofstream file(meta_path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.put(static_cast<char>(0xff));
    file.close();

    db::TableDescriptor desc;
    db::Status status = manager.GetTable("testdb", "broken", &desc);

    EXPECT_FALSE(status.ok());
}

}  // namespace
