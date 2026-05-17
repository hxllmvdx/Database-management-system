#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "common/config.h"
#include "runtime/storage_node_engine.h"
#include "versioning/revert.h"

namespace {

namespace fs = std::filesystem;

class StorageNodeEngineTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPageSize = 65536;

    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_storage_node_engine_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::StorageNodeEngine CreateStartedEngine() const {
        db::Config config;
        config.data_dir = root_dir_.string();
        config.page_size = kPageSize;

        db::StorageNodeEngine engine(config);
        EXPECT_TRUE(engine.Start().ok());
        return engine;
    }

    db::TableDescriptor MakeUsersTable(bool name_indexed = true) const {
        db::TableDescriptor desc;
        desc.database_name = "testdb";
        desc.table_name = "users";
        desc.data_file = (root_dir_ / "testdb" / "tables" / "users.tbl").string();
        desc.index_dir = (root_dir_ / "testdb" / "indexes" / "users").string();

        db::ColumnSchema id_column;
        id_column.name = "id";
        id_column.type = db::ColumnType::kInt;
        id_column.not_null = true;

        db::ColumnSchema name_column;
        name_column.name = "name";
        name_column.type = db::ColumnType::kString;
        name_column.not_null = false;

        db::ColumnSchema active_column;
        active_column.name = "active";
        active_column.type = db::ColumnType::kBool;
        active_column.default_value = db::Value::Bool(true);

        desc.schema.columns = {id_column, name_column, active_column};

        db::IndexDescriptor id_index;
        id_index.name = "users_id_idx";
        id_index.table_name = "users";
        id_index.column_name = "id";
        id_index.file_path = (root_dir_ / "testdb" / "indexes" / "users" / "users_id_idx.idx").string();
        id_index.unique = true;
        desc.indexes.push_back(id_index);

        if (name_indexed) {
            db::IndexDescriptor name_index;
            name_index.name = "users_name_idx";
            name_index.table_name = "users";
            name_index.column_name = "name";
            name_index.file_path =
                (root_dir_ / "testdb" / "indexes" / "users" / "users_name_idx.idx").string();
            name_index.unique = true;
            desc.indexes.push_back(name_index);
        }

        return desc;
    }

    static db::Tuple MakeTuple(std::initializer_list<db::Value> values) {
        db::Tuple tuple;
        tuple.values.assign(values.begin(), values.end());
        return tuple;
    }

    static void ExpectTupleEquals(const db::Tuple& actual, const db::Tuple& expected) {
        ASSERT_EQ(actual.values.size(), expected.values.size());
        for (std::size_t i = 0; i < actual.values.size(); ++i) {
            EXPECT_EQ(actual.values[i], expected.values[i]);
        }
    }

    static void AdvanceClock() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::vector<db::VersionRecord> ReadVersionLog(const fs::path& path) const {
        db::VersionLog log(path.string());
        EXPECT_TRUE(log.Open().ok());
        std::vector<db::VersionRecord> records;
        EXPECT_TRUE(log.ReadAll(&records).ok());
        return records;
    }

    fs::path VersionLogPath() const {
        return root_dir_ / "testdb" / "versions" / "users.vlog";
    }

    fs::path root_dir_;
};

TEST_F(StorageNodeEngineTest, CreateTableThenInsertWritesRowAndVersion) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &rid).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].rid, rid);
    ExpectTupleEquals(rows[0].tuple,
                      MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));

    ASSERT_TRUE(engine.Stop().ok());
    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records[0].table_name, "users");
    EXPECT_EQ(records[0].rid, rid);
    EXPECT_EQ(records[0].op, db::VersionOp::kInsert);
    EXPECT_TRUE(records[0].before.values.empty());
    ExpectTupleEquals(records[0].after,
                      MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));
}

TEST_F(StorageNodeEngineTest, InsertCreatesIndexEntries) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(7), db::Value::String("bob")}), &rid).ok());

    std::optional<db::RowId> found_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(7), &found_id).ok());
    ASSERT_TRUE(found_id.has_value());
    EXPECT_EQ(*found_id, rid);

    std::optional<db::RowId> found_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("bob"), &found_name).ok());
    ASSERT_TRUE(found_name.has_value());
    EXPECT_EQ(*found_name, rid);
}

TEST_F(StorageNodeEngineTest, UpdateRewritesIndexesAndAppendsVersion) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &rid).ok());

    ASSERT_TRUE(engine.Update("testdb", "users", rid, MakeTuple({db::Value::Int(2), db::Value::String("betty"), db::Value::Bool(false)})).ok());

    std::optional<db::RowId> old_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(1), &old_id).ok());
    EXPECT_FALSE(old_id.has_value());

    std::optional<db::RowId> new_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(2), &new_id).ok());
    ASSERT_TRUE(new_id.has_value());
    EXPECT_EQ(*new_id, rid);

    std::optional<db::RowId> old_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("alice"), &old_name).ok());
    EXPECT_FALSE(old_name.has_value());

    std::optional<db::RowId> new_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("betty"), &new_name).ok());
    ASSERT_TRUE(new_name.has_value());
    EXPECT_EQ(*new_name, rid);

    ASSERT_TRUE(engine.Stop().ok());
    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[1].op, db::VersionOp::kUpdate);
    ExpectTupleEquals(records[1].before,
                      MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));
    ExpectTupleEquals(records[1].after,
                      MakeTuple({db::Value::Int(2), db::Value::String("betty"), db::Value::Bool(false)}));
}

TEST_F(StorageNodeEngineTest, DeleteRemovesIndexEntriesAndAppendsVersion) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(3), db::Value::String("carol")}), &rid).ok());

    ASSERT_TRUE(engine.Delete("testdb", "users", rid).ok());

    std::optional<db::RowId> found_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(3), &found_id).ok());
    EXPECT_FALSE(found_id.has_value());

    std::optional<db::RowId> found_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("carol"), &found_name).ok());
    EXPECT_FALSE(found_name.has_value());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    EXPECT_TRUE(rows.empty());

    ASSERT_TRUE(engine.Stop().ok());
    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[1].op, db::VersionOp::kDelete);
    ExpectTupleEquals(records[1].before,
                      MakeTuple({db::Value::Int(3), db::Value::String("carol"), db::Value::Bool(true)}));
    EXPECT_TRUE(records[1].after.values.empty());
}

TEST_F(StorageNodeEngineTest, ScanTableDelegatesToTableStorage) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId first_rid;
    db::RowId second_rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("a")}), &first_rid).ok());
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(2), db::Value::String("b")}), &second_rid).ok());
    ASSERT_TRUE(engine.Delete("testdb", "users", first_rid).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].rid, second_rid);
    ExpectTupleEquals(rows[0].tuple,
                      MakeTuple({db::Value::Int(2), db::Value::String("b"), db::Value::Bool(true)}));
}

TEST_F(StorageNodeEngineTest, OpenMissingTableFails) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());

    std::vector<db::Row> rows;
    const db::Status status = engine.ScanTable("testdb", "missing", &rows);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kNotFound);
}

TEST_F(StorageNodeEngineTest, NullIndexedValueIsSkippedByIndexMaintenance) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(5), db::Value::Null()}), &rid).ok());

    std::optional<db::RowId> found_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("nobody"), &found_name).ok());
    EXPECT_FALSE(found_name.has_value());

    ASSERT_TRUE(engine.Update("testdb", "users", rid, MakeTuple({db::Value::Int(5), db::Value::String("dora"), db::Value::Bool(true)})).ok());
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("dora"), &found_name).ok());
    ASSERT_TRUE(found_name.has_value());
    EXPECT_EQ(*found_name, rid);

    ASSERT_TRUE(engine.Update("testdb", "users", rid, MakeTuple({db::Value::Int(5), db::Value::Null(), db::Value::Bool(false)})).ok());
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("dora"), &found_name).ok());
    EXPECT_FALSE(found_name.has_value());
}

TEST_F(StorageNodeEngineTest, EngineStopClearsCachesAndReopenStillWorks) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    const db::Status create_status = engine.CreateTable(MakeUsersTable());
    ASSERT_TRUE(create_status.ok()) << create_status.message();

    db::RowId first_rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(9), db::Value::String("persist")}), &first_rid).ok());

    ASSERT_TRUE(engine.Stop().ok());
    ASSERT_TRUE(engine.Start().ok());

    db::RowId second_rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(10), db::Value::String("again")}), &second_rid).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 2U);

    std::optional<db::RowId> found;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(10), &found).ok());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, second_rid);
}

TEST_F(StorageNodeEngineTest, RevertAfterInsertDeletesInsertedRow) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    ASSERT_TRUE(engine.CreateTable(MakeUsersTable()).ok());

    const std::int64_t before_insert = 0;
    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &rid).ok());

    db::RevertService revert(&engine);
    ASSERT_TRUE(revert.RevertTableToTimestamp("testdb", "users", before_insert).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    EXPECT_TRUE(rows.empty());

    std::optional<db::RowId> found;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(1), &found).ok());
    EXPECT_FALSE(found.has_value());

    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 1U);
}

TEST_F(StorageNodeEngineTest, RevertAfterDeleteRestoresRowWithSameRid) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    ASSERT_TRUE(engine.CreateTable(MakeUsersTable()).ok());

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(3), db::Value::String("carol")}), &rid).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Delete("testdb", "users", rid).ok());

    const std::vector<db::VersionRecord> before_revert = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(before_revert.size(), 2U);

    db::RevertService revert(&engine);
    ASSERT_TRUE(revert.RevertTableToTimestamp("testdb", "users", before_revert[0].timestamp_ms).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].rid, rid);
    ExpectTupleEquals(rows[0].tuple,
                      MakeTuple({db::Value::Int(3), db::Value::String("carol"), db::Value::Bool(true)}));

    std::optional<db::RowId> found;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("carol"), &found).ok());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, rid);

    const std::vector<db::VersionRecord> after_revert = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(after_revert.size(), before_revert.size());
}

TEST_F(StorageNodeEngineTest, RevertAfterUpdateRestoresPreviousTuple) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    ASSERT_TRUE(engine.CreateTable(MakeUsersTable()).ok());

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &rid).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Update("testdb", "users", rid,
                              MakeTuple({db::Value::Int(2), db::Value::String("betty"), db::Value::Bool(false)})).ok());

    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 2U);

    db::RevertService revert(&engine);
    ASSERT_TRUE(revert.RevertTableToTimestamp("testdb", "users", records[0].timestamp_ms).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].rid, rid);
    ExpectTupleEquals(rows[0].tuple,
                      MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));
}

TEST_F(StorageNodeEngineTest, RevertMultipleOperationsReplaysInReverseOrder) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    ASSERT_TRUE(engine.CreateTable(MakeUsersTable()).ok());

    db::RowId alice_rid;
    db::RowId bob_rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &alice_rid).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(2), db::Value::String("bob")}), &bob_rid).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Update("testdb", "users", alice_rid,
                              MakeTuple({db::Value::Int(10), db::Value::String("alice-2"), db::Value::Bool(false)})).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Delete("testdb", "users", bob_rid).ok());

    const std::vector<db::VersionRecord> records = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(records.size(), 4U);

    db::RevertService revert(&engine);
    ASSERT_TRUE(revert.RevertTableToTimestamp("testdb", "users", records[1].timestamp_ms).ok());

    std::vector<db::Row> rows;
    ASSERT_TRUE(engine.ScanTable("testdb", "users", &rows).ok());
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].rid, alice_rid);
    ExpectTupleEquals(rows[0].tuple,
                      MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));
    EXPECT_EQ(rows[1].rid, bob_rid);
    ExpectTupleEquals(rows[1].tuple,
                      MakeTuple({db::Value::Int(2), db::Value::String("bob"), db::Value::Bool(true)}));
}

TEST_F(StorageNodeEngineTest, RevertRestoresIndexesConsistentlyAndDoesNotAppendVersionRecords) {
    db::StorageNodeEngine engine = CreateStartedEngine();
    ASSERT_TRUE(engine.CreateDatabase("testdb").ok());
    ASSERT_TRUE(engine.CreateTable(MakeUsersTable()).ok());

    db::RowId rid;
    ASSERT_TRUE(engine.Insert("testdb", "users", MakeTuple({db::Value::Int(5), db::Value::String("dora")}), &rid).ok());
    AdvanceClock();
    ASSERT_TRUE(engine.Update("testdb", "users", rid,
                              MakeTuple({db::Value::Int(6), db::Value::String("eva"), db::Value::Bool(false)})).ok());

    const std::vector<db::VersionRecord> before_revert = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(before_revert.size(), 2U);

    db::RevertService revert(&engine);
    ASSERT_TRUE(revert.RevertTableToTimestamp("testdb", "users", before_revert[0].timestamp_ms).ok());

    std::optional<db::RowId> old_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(5), &old_id).ok());
    ASSERT_TRUE(old_id.has_value());
    EXPECT_EQ(*old_id, rid);

    std::optional<db::RowId> new_id;
    ASSERT_TRUE(engine.index_manager().Find("users_id_idx", db::Value::Int(6), &new_id).ok());
    EXPECT_FALSE(new_id.has_value());

    std::optional<db::RowId> old_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("dora"), &old_name).ok());
    ASSERT_TRUE(old_name.has_value());
    EXPECT_EQ(*old_name, rid);

    std::optional<db::RowId> new_name;
    ASSERT_TRUE(engine.index_manager().Find("users_name_idx", db::Value::String("eva"), &new_name).ok());
    EXPECT_FALSE(new_name.has_value());

    const std::vector<db::VersionRecord> after_revert = ReadVersionLog(VersionLogPath());
    ASSERT_EQ(after_revert.size(), before_revert.size());
}

}  // namespace
