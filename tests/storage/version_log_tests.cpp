#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "versioning/version_log.h"

namespace {

namespace fs = std::filesystem;

class VersionLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_version_log_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
        ASSERT_TRUE(fs::create_directories(root_dir_));
        file_path_ = root_dir_ / "users.vlog";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::VersionLog CreateLog() const {
        return db::VersionLog(file_path_.string());
    }

    static db::Tuple MakeTuple(std::initializer_list<db::Value> values) {
        db::Tuple tuple;
        tuple.values.assign(values.begin(), values.end());
        return tuple;
    }

    static db::VersionRecord MakeRecord(db::VersionOp op,
                                        std::int64_t ts,
                                        std::uint64_t rid,
                                        db::Tuple before,
                                        db::Tuple after) {
        db::VersionRecord record;
        record.timestamp_ms = ts;
        record.table_name = "users";
        record.rid = db::RowId{rid};
        record.op = op;
        record.before = std::move(before);
        record.after = std::move(after);
        return record;
    }

    static void ExpectTupleEquals(const db::Tuple& actual, const db::Tuple& expected) {
        ASSERT_EQ(actual.values.size(), expected.values.size());
        for (std::size_t i = 0; i < actual.values.size(); ++i) {
            EXPECT_EQ(actual.values[i], expected.values[i]);
        }
    }

    static void ExpectRecordEquals(const db::VersionRecord& actual, const db::VersionRecord& expected) {
        EXPECT_EQ(actual.timestamp_ms, expected.timestamp_ms);
        EXPECT_EQ(actual.table_name, expected.table_name);
        EXPECT_EQ(actual.rid, expected.rid);
        EXPECT_EQ(actual.op, expected.op);
        ExpectTupleEquals(actual.before, expected.before);
        ExpectTupleEquals(actual.after, expected.after);
    }

    fs::path root_dir_;
    fs::path file_path_;
};

TEST_F(VersionLogTest, OpenCreatesLogAndReadAllOnEmptyFileReturnsNoRecords) {
    db::VersionLog log = CreateLog();

    ASSERT_TRUE(log.Open().ok());
    EXPECT_TRUE(fs::exists(file_path_));

    std::vector<db::VersionRecord> records;
    ASSERT_TRUE(log.ReadAll(&records).ok());
    EXPECT_TRUE(records.empty());
}

TEST_F(VersionLogTest, AppendAndReadAllPreserveInsertUpdateDeleteRecords) {
    db::VersionLog log = CreateLog();
    ASSERT_TRUE(log.Open().ok());

    const db::VersionRecord insert_record =
        MakeRecord(db::VersionOp::kInsert,
                   1000,
                   0,
                   MakeTuple({}),
                   MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}));
    const db::VersionRecord update_record =
        MakeRecord(db::VersionOp::kUpdate,
                   2000,
                   0,
                   MakeTuple({db::Value::Int(1), db::Value::String("alice"), db::Value::Bool(true)}),
                   MakeTuple({db::Value::Int(1), db::Value::String("alice-updated"), db::Value::Bool(false)}));
    const db::VersionRecord delete_record =
        MakeRecord(db::VersionOp::kDelete,
                   3000,
                   0,
                   MakeTuple({db::Value::Int(1), db::Value::String("alice-updated"), db::Value::Bool(false)}),
                   MakeTuple({}));

    ASSERT_TRUE(log.Append(insert_record).ok());
    ASSERT_TRUE(log.Append(update_record).ok());
    ASSERT_TRUE(log.Append(delete_record).ok());
    ASSERT_TRUE(log.Flush().ok());

    std::vector<db::VersionRecord> records;
    ASSERT_TRUE(log.ReadAll(&records).ok());
    ASSERT_EQ(records.size(), 3U);
    ExpectRecordEquals(records[0], insert_record);
    ExpectRecordEquals(records[1], update_record);
    ExpectRecordEquals(records[2], delete_record);
}

TEST_F(VersionLogTest, ReopenPreservesAppendedRecords) {
    const db::VersionRecord record =
        MakeRecord(db::VersionOp::kInsert,
                   4242,
                   7,
                   MakeTuple({}),
                   MakeTuple({db::Value::Int(7), db::Value::String("persist"), db::Value::Bool(true)}));

    {
        db::VersionLog log = CreateLog();
        ASSERT_TRUE(log.Open().ok());
        ASSERT_TRUE(log.Append(record).ok());
        ASSERT_TRUE(log.Flush().ok());
    }

    db::VersionLog reopened = CreateLog();
    ASSERT_TRUE(reopened.Open().ok());

    std::vector<db::VersionRecord> records;
    ASSERT_TRUE(reopened.ReadAll(&records).ok());
    ASSERT_EQ(records.size(), 1U);
    ExpectRecordEquals(records[0], record);
}

TEST_F(VersionLogTest, OpenRejectsInvalidHeader) {
    std::ofstream file(file_path_, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file.write("notalog!", 8);
    file.close();

    db::VersionLog log = CreateLog();
    db::Status status = log.Open();

    EXPECT_FALSE(status.ok());
}

TEST_F(VersionLogTest, ReadAllRejectsTruncatedRecordPayload) {
    db::VersionLog log = CreateLog();
    ASSERT_TRUE(log.Open().ok());

    const db::VersionRecord record =
        MakeRecord(db::VersionOp::kInsert,
                   1234,
                   1,
                   MakeTuple({}),
                   MakeTuple({db::Value::Int(1), db::Value::String("broken"), db::Value::Bool(true)}));
    ASSERT_TRUE(log.Append(record).ok());
    ASSERT_TRUE(log.Flush().ok());

    std::uintmax_t original_size = fs::file_size(file_path_);
    ASSERT_GT(original_size, 1U);
    fs::resize_file(file_path_, original_size - 1U);

    db::VersionLog reopened = CreateLog();
    ASSERT_TRUE(reopened.Open().ok());

    std::vector<db::VersionRecord> records;
    db::Status status = reopened.ReadAll(&records);
    EXPECT_FALSE(status.ok());
}

}  
