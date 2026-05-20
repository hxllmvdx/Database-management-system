#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "index/bstar_tree.h"

namespace {

namespace fs = std::filesystem;

class BStarTreeTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPageSize = 256;
    static constexpr std::size_t kMinDegree = 2;

    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_bstar_tree_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
        ASSERT_TRUE(fs::create_directories(root_dir_) || fs::exists(root_dir_));
        file_path_ = root_dir_ / "users.idx";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::BStarTree CreateIntTree() const {
        return db::BStarTree(file_path_.string(), db::ValueType::kInt, kPageSize, kMinDegree);
    }

    static db::Value Int(std::int64_t value) {
        return db::Value::Int(value);
    }

    fs::path root_dir_;
    fs::path file_path_;
};

TEST_F(BStarTreeTest, OpenCreatesEmptyTreeAndFindReturnsNullopt) {
    db::BStarTree tree = CreateIntTree();
    ASSERT_TRUE(tree.Open().ok());

    std::optional<db::RowId> rid;
    ASSERT_TRUE(tree.Find(Int(10), &rid).ok());
    EXPECT_FALSE(rid.has_value());
}

TEST_F(BStarTreeTest, InsertFindAndReopenPreserveEntriesAfterRootSplit) {
    {
        db::BStarTree tree = CreateIntTree();
        ASSERT_TRUE(tree.Open().ok());
        for (std::uint64_t i = 1; i <= 6; ++i) {
            ASSERT_TRUE(tree.Insert(Int(static_cast<std::int64_t>(i)), db::RowId{i * 10U}).ok());
        }

        for (std::uint64_t i = 1; i <= 6; ++i) {
            std::optional<db::RowId> rid;
            ASSERT_TRUE(tree.Find(Int(static_cast<std::int64_t>(i)), &rid).ok());
            ASSERT_TRUE(rid.has_value());
            EXPECT_EQ(*rid, db::RowId{i * 10U});
        }
    }

    db::BStarTree reopened = CreateIntTree();
    ASSERT_TRUE(reopened.Open().ok());
    for (std::uint64_t i = 1; i <= 6; ++i) {
        std::optional<db::RowId> rid;
        ASSERT_TRUE(reopened.Find(Int(static_cast<std::int64_t>(i)), &rid).ok());
        ASSERT_TRUE(rid.has_value());
        EXPECT_EQ(*rid, db::RowId{i * 10U});
    }
}

TEST_F(BStarTreeTest, RangeSearchReturnsOrderedRids) {
    db::BStarTree tree = CreateIntTree();
    ASSERT_TRUE(tree.Open().ok());
    for (std::uint64_t i = 1; i <= 6; ++i) {
        ASSERT_TRUE(tree.Insert(Int(static_cast<std::int64_t>(i)), db::RowId{100U + i}).ok());
    }

    db::KeyRange range;
    range.low = Int(2);
    range.high = Int(5);
    range.include_low = true;
    range.include_high = false;

    std::vector<db::RowId> rids;
    ASSERT_TRUE(tree.RangeSearch(range, &rids).ok());
    ASSERT_EQ(rids.size(), 3U);
    EXPECT_EQ(rids[0], db::RowId{102});
    EXPECT_EQ(rids[1], db::RowId{103});
    EXPECT_EQ(rids[2], db::RowId{104});
}

TEST_F(BStarTreeTest, DuplicateAndWrongTypeKeysAreRejected) {
    db::BStarTree tree = CreateIntTree();
    ASSERT_TRUE(tree.Open().ok());

    ASSERT_TRUE(tree.Insert(Int(1), db::RowId{10}).ok());

    db::Status duplicate = tree.Insert(Int(1), db::RowId{11});
    EXPECT_FALSE(duplicate.ok());

    db::Status wrong_type = tree.Insert(db::Value::String("bad"), db::RowId{12});
    EXPECT_FALSE(wrong_type.ok());
}

TEST_F(BStarTreeTest, DeleteRemovesPromotedInternalKey) {
    db::BStarTree tree = CreateIntTree();
    ASSERT_TRUE(tree.Open().ok());
    for (std::uint64_t i = 1; i <= 4; ++i) {
        ASSERT_TRUE(tree.Insert(Int(static_cast<std::int64_t>(i)), db::RowId{i}).ok());
    }

    ASSERT_TRUE(tree.Delete(Int(3)).ok());

    std::optional<db::RowId> rid;
    ASSERT_TRUE(tree.Find(Int(3), &rid).ok());
    EXPECT_FALSE(rid.has_value());

    for (std::uint64_t i : {1U, 2U, 4U}) {
        ASSERT_TRUE(tree.Find(Int(static_cast<std::int64_t>(i)), &rid).ok());
        ASSERT_TRUE(rid.has_value());
        EXPECT_EQ(*rid, db::RowId{i});
    }
}

TEST_F(BStarTreeTest, ManyInsertsAndDeletesKeepRemainingKeysReachableAfterReopen) {
    {
        db::BStarTree tree = CreateIntTree();
        ASSERT_TRUE(tree.Open().ok());
        for (std::uint64_t i = 1; i <= 20; ++i) {
            ASSERT_TRUE(tree.Insert(Int(static_cast<std::int64_t>(i)), db::RowId{1000U + i}).ok());
        }

        for (std::uint64_t i : {2U, 3U, 5U, 8U, 13U, 18U}) {
            ASSERT_TRUE(tree.Delete(Int(static_cast<std::int64_t>(i))).ok());
        }
    }

    db::BStarTree reopened = CreateIntTree();
    ASSERT_TRUE(reopened.Open().ok());

    for (std::uint64_t i = 1; i <= 20; ++i) {
        std::optional<db::RowId> rid;
        ASSERT_TRUE(reopened.Find(Int(static_cast<std::int64_t>(i)), &rid).ok());
        const bool deleted = i == 2U || i == 3U || i == 5U || i == 8U || i == 13U || i == 18U;
        if (deleted) {
            EXPECT_FALSE(rid.has_value());
        } else {
            ASSERT_TRUE(rid.has_value());
            EXPECT_EQ(*rid, db::RowId{1000U + i});
        }
    }
}

}  // namespace
