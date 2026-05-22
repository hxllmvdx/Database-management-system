#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "storage/table_storage.h"

namespace {

namespace fs = std::filesystem;

class TableStorageTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPageSize = 512;

    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_table_storage_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
        ASSERT_TRUE(fs::create_directories(root_dir_));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::TableDescriptor MakeDescriptor() const {
        db::TableDescriptor desc;
        desc.database_name = "testdb";
        desc.table_name = "users";
        desc.data_file = (root_dir_ / "users.tbl").string();
        desc.index_dir = (root_dir_ / "indexes" / "users").string();

        db::ColumnSchema id_column;
        id_column.name = "id";
        id_column.type = db::ColumnType::kInt;
        id_column.not_null = true;

        db::ColumnSchema name_column;
        name_column.name = "name";
        name_column.type = db::ColumnType::kString;
        name_column.default_value = db::Value::String("guest");

        db::ColumnSchema active_column;
        active_column.name = "active";
        active_column.type = db::ColumnType::kBool;
        active_column.default_value = db::Value::Bool(true);

        db::ColumnSchema note_column;
        note_column.name = "note";
        note_column.type = db::ColumnType::kString;
        note_column.not_null = false;

        desc.schema.columns = {id_column, name_column, active_column, note_column};
        return desc;
    }

    db::TableStorage CreateStorage() const {
        return db::TableStorage(MakeDescriptor(), kPageSize);
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

    fs::path root_dir_;
};

TEST_F(TableStorageTest, InsertThenGetRoundTripAppliesDefaultsAndNulls) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(1)}), &rid).ok());

    db::Row row;
    ASSERT_TRUE(storage.Get(rid, &row).ok());
    ExpectTupleEquals(row.tuple,
                      MakeTuple({db::Value::Int(1),
                                 db::Value::String("guest"),
                                 db::Value::Bool(true),
                                 db::Value::Null()}));
}

TEST_F(TableStorageTest, InsertRejectsTooManyValues) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    db::Status status = storage.Insert(
        MakeTuple({db::Value::Int(1),
                   db::Value::String("alice"),
                   db::Value::Bool(false),
                   db::Value::String("note"),
                   db::Value::Int(99)}),
        &rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

TEST_F(TableStorageTest, InsertRejectsWrongType) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    db::Status status = storage.Insert(MakeTuple({db::Value::String("bad-id")}), &rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

TEST_F(TableStorageTest, InsertRejectsNullForNotNullColumn) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    db::Status status = storage.Insert(MakeTuple({db::Value::Null()}), &rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

TEST_F(TableStorageTest, UpdateUsesSameValidationRulesAsInsert) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(1), db::Value::String("alice")}), &rid).ok());

    db::Status bad_status = storage.Update(rid, MakeTuple({db::Value::Int(1), db::Value::Bool(false)}));
    EXPECT_FALSE(bad_status.ok());

    ASSERT_TRUE(storage.Update(rid, MakeTuple({db::Value::Int(2), db::Value::String("bob")})).ok());

    db::Row row;
    ASSERT_TRUE(storage.Get(rid, &row).ok());
    ExpectTupleEquals(row.tuple,
                      MakeTuple({db::Value::Int(2),
                                 db::Value::String("bob"),
                                 db::Value::Bool(true),
                                 db::Value::Null()}));
}

TEST_F(TableStorageTest, DeleteMakesRowUnavailableAndScanReturnsOnlyLiveRows) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId first_rid;
    db::RowId second_rid;
    ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(1)}), &first_rid).ok());
    ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(2), db::Value::String("bob")}), &second_rid).ok());

    ASSERT_TRUE(storage.Delete(first_rid).ok());

    db::Row deleted_row;
    db::Status get_status = storage.Get(first_rid, &deleted_row);
    EXPECT_FALSE(get_status.ok());
    EXPECT_EQ(get_status.code(), db::StatusCode::kNotFound);

    std::vector<db::Row> rows;
    ASSERT_TRUE(storage.Scan(&rows).ok());
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].rid, second_rid);
}

TEST_F(TableStorageTest, RestoreRevivesDeletedRowWithSameRidAndValidation) {
    db::TableStorage storage = CreateStorage();
    ASSERT_TRUE(storage.Open().ok());

    db::RowId rid;
    ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(1), db::Value::String("before")}), &rid).ok());
    ASSERT_TRUE(storage.Delete(rid).ok());

    ASSERT_TRUE(storage.Restore(rid, MakeTuple({db::Value::Int(2), db::Value::String("after")})).ok());

    db::Row row;
    ASSERT_TRUE(storage.Get(rid, &row).ok());
    ExpectTupleEquals(row.tuple,
                      MakeTuple({db::Value::Int(2),
                                 db::Value::String("after"),
                                 db::Value::Bool(true),
                                 db::Value::Null()}));
}

TEST_F(TableStorageTest, ReopenPreservesRows) {
    db::RowId rid;

    {
        db::TableStorage storage = CreateStorage();
        ASSERT_TRUE(storage.Open().ok());
        ASSERT_TRUE(storage.Insert(MakeTuple({db::Value::Int(7), db::Value::String("persist")}), &rid).ok());
    }

    db::TableStorage reopened = CreateStorage();
    ASSERT_TRUE(reopened.Open().ok());

    db::Row row;
    ASSERT_TRUE(reopened.Get(rid, &row).ok());
    ExpectTupleEquals(row.tuple,
                      MakeTuple({db::Value::Int(7),
                                 db::Value::String("persist"),
                                 db::Value::Bool(true),
                                 db::Value::Null()}));
}

}  
