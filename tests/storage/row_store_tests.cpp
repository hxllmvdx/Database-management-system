#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "storage/row_store.h"

namespace {

namespace fs = std::filesystem;

class RowStoreTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPageSize = 512;

    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_row_store_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
        ASSERT_TRUE(fs::create_directories(root_dir_));
        file_path_ = root_dir_ / "rows.db";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::RowStore CreateStore() const {
        return db::RowStore(file_path_.string(), kPageSize);
    }

    static db::Tuple MakeTuple(std::int64_t id, const std::string& name, bool active) {
        db::Tuple tuple;
        tuple.values.push_back(db::Value::Int(id));
        tuple.values.push_back(db::Value::String(name));
        tuple.values.push_back(db::Value::Bool(active));
        return tuple;
    }

    static void ExpectRowEquals(const db::Row& actual, const db::Row& expected) {
        EXPECT_EQ(actual.rid, expected.rid);
        EXPECT_EQ(actual.deleted, expected.deleted);
        ASSERT_EQ(actual.tuple.values.size(), expected.tuple.values.size());
        for (std::size_t i = 0; i < actual.tuple.values.size(); ++i) {
            EXPECT_EQ(actual.tuple.values[i], expected.tuple.values[i]);
        }
    }

    fs::path root_dir_;
    fs::path file_path_;
};

TEST_F(RowStoreTest, InsertGetAndScanAssignStableIds) {
    db::RowStore store = CreateStore();
    ASSERT_TRUE(store.Open().ok());

    db::RowId first_id;
    db::RowId second_id;
    ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(1, "alice", true), false}, &first_id).ok());
    ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(2, "bob", false), false}, &second_id).ok());

    EXPECT_EQ(first_id, db::RowId{0});
    EXPECT_EQ(second_id, db::RowId{1});

    db::Row first_row;
    ASSERT_TRUE(store.Get(first_id, &first_row).ok());
    ExpectRowEquals(first_row, db::Row{first_id, MakeTuple(1, "alice", true), false});

    std::vector<db::Row> rows;
    ASSERT_TRUE(store.Scan(&rows).ok());
    ASSERT_EQ(rows.size(), 2U);
    ExpectRowEquals(rows[0], db::Row{first_id, MakeTuple(1, "alice", true), false});
    ExpectRowEquals(rows[1], db::Row{second_id, MakeTuple(2, "bob", false), false});
}

TEST_F(RowStoreTest, DeleteHidesRowButKeepsItInIncludingDeletedViews) {
    db::RowStore store = CreateStore();
    ASSERT_TRUE(store.Open().ok());

    db::RowId rid;
    ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(10, "ghost", true), false}, &rid).ok());
    ASSERT_TRUE(store.Delete(rid).ok());

    db::Row row;
    db::Status get_status = store.Get(rid, &row);
    EXPECT_FALSE(get_status.ok());
    EXPECT_EQ(get_status.code(), db::StatusCode::kNotFound);

    ASSERT_TRUE(store.GetIncludingDeleted(rid, &row).ok());
    ExpectRowEquals(row, db::Row{rid, MakeTuple(10, "ghost", true), true});

    std::vector<db::Row> live_rows;
    ASSERT_TRUE(store.Scan(&live_rows).ok());
    EXPECT_TRUE(live_rows.empty());

    std::vector<db::Row> all_rows;
    ASSERT_TRUE(store.ScanIncludingDeleted(&all_rows).ok());
    ASSERT_EQ(all_rows.size(), 1U);
    ExpectRowEquals(all_rows[0], db::Row{rid, MakeTuple(10, "ghost", true), true});
}

TEST_F(RowStoreTest, RestoreRevivesDeletedRowWithSameRid) {
    db::RowStore store = CreateStore();
    ASSERT_TRUE(store.Open().ok());

    db::RowId rid;
    ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(3, "before", true), false}, &rid).ok());
    ASSERT_TRUE(store.Delete(rid).ok());

    db::Row restored{rid, MakeTuple(3, "after", false), true};
    ASSERT_TRUE(store.Restore(restored).ok());

    db::Row actual;
    ASSERT_TRUE(store.Get(rid, &actual).ok());
    ExpectRowEquals(actual, db::Row{rid, MakeTuple(3, "after", false), false});

    std::vector<db::Row> live_rows;
    ASSERT_TRUE(store.Scan(&live_rows).ok());
    ASSERT_EQ(live_rows.size(), 1U);
    ExpectRowEquals(live_rows[0], db::Row{rid, MakeTuple(3, "after", false), false});
}

TEST_F(RowStoreTest, RestoreMissingRowAdvancesAllocatorForNextInsert) {
    db::RowStore store = CreateStore();
    ASSERT_TRUE(store.Open().ok());

    ASSERT_TRUE(store.Restore(db::Row{db::RowId{7}, MakeTuple(7, "restored", true), false}).ok());

    db::Row restored;
    ASSERT_TRUE(store.Get(db::RowId{7}, &restored).ok());
    ExpectRowEquals(restored, db::Row{db::RowId{7}, MakeTuple(7, "restored", true), false});

    db::RowId next_id;
    ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(8, "next", false), false}, &next_id).ok());
    EXPECT_EQ(next_id, db::RowId{8});
}

TEST_F(RowStoreTest, UpdateAndReopenPreserveState) {
    db::RowId updated_rid;

    {
        db::RowStore store = CreateStore();
        ASSERT_TRUE(store.Open().ok());

        db::RowId first_id;
        db::RowId second_id;
        ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(1, "alpha", true), false}, &first_id).ok());
        ASSERT_TRUE(store.Insert(db::Row{db::RowId{}, MakeTuple(2, "beta", false), false}, &second_id).ok());
        ASSERT_TRUE(store.Update(second_id, MakeTuple(22, "beta-updated", true)).ok());
        ASSERT_TRUE(store.Flush().ok());
        updated_rid = second_id;
    }

    db::RowStore reopened = CreateStore();
    ASSERT_TRUE(reopened.Open().ok());

    db::Row updated;
    ASSERT_TRUE(reopened.Get(updated_rid, &updated).ok());
    ExpectRowEquals(updated, db::Row{updated_rid, MakeTuple(22, "beta-updated", true), false});

    db::RowId new_id;
    ASSERT_TRUE(reopened.Insert(db::Row{db::RowId{}, MakeTuple(3, "gamma", true), false}, &new_id).ok());
    EXPECT_EQ(new_id, db::RowId{2});
}

}  // namespace
