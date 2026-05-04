#include <filesystem>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "index/index_manager.h"

namespace {

namespace fs = std::filesystem;

class IndexManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_dir_ = fs::temp_directory_path() / "coursedb_index_manager_tests" /
                    ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
        ASSERT_FALSE(fs::exists(root_dir_));
        ASSERT_TRUE(fs::create_directories(root_dir_) || fs::exists(root_dir_));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_dir_, ec);
    }

    db::IndexDescriptor MakeIndex(const std::string& name,
                                  const std::string& table_name,
                                  const std::string& column_name) const {
        db::IndexDescriptor desc;
        desc.name = name;
        desc.table_name = table_name;
        desc.column_name = column_name;
        desc.file_path = (root_dir_ / (name + ".idx")).string();
        desc.unique = true;
        return desc;
    }

    fs::path root_dir_;
};

TEST_F(IndexManagerTest, ComputeMinDegreeUsesPageLayoutRatherThanFallbackTwo) {
    std::size_t max_int_key_size = 0;
    ASSERT_TRUE(db::BStarTree::ResolveMaxEncodedKeySize(
                    db::ValueType::kInt,
                    db::BStarTree::kDefaultMaxVariableKeyPayloadBytes,
                    &max_int_key_size)
                    .ok());
    EXPECT_EQ(max_int_key_size, sizeof(std::int64_t));

    std::size_t min_degree = 0;
    ASSERT_TRUE(db::BStarTree::ComputeMinDegreeForPage(512U, max_int_key_size, &min_degree).ok());
    EXPECT_GT(min_degree, 2U);
}

TEST_F(IndexManagerTest, CreateIndexInsertFindAndRangeSearchRoundTrip) {
    db::IndexManager manager(512U);
    const db::IndexDescriptor desc = MakeIndex("users_id_idx", "users", "id");

    ASSERT_TRUE(manager.CreateIndex(desc, db::ValueType::kInt).ok());
    ASSERT_TRUE(manager.Insert(desc.name, db::Value::Int(10), db::RowId{1}).ok());
    ASSERT_TRUE(manager.Insert(desc.name, db::Value::Int(20), db::RowId{2}).ok());
    ASSERT_TRUE(manager.Insert(desc.name, db::Value::Int(30), db::RowId{3}).ok());

    std::optional<db::RowId> found;
    ASSERT_TRUE(manager.Find(desc.name, db::Value::Int(20), &found).ok());
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value, 2U);

    db::KeyRange range;
    range.low = db::Value::Int(15);
    range.high = db::Value::Int(31);
    range.include_low = true;
    range.include_high = false;

    std::vector<db::RowId> matches;
    ASSERT_TRUE(manager.RangeSearch(desc.name, range, &matches).ok());
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].value, 2U);
    EXPECT_EQ(matches[1].value, 3U);
}

TEST_F(IndexManagerTest, OpenIndexRequiresExistingFileAndReopenPreservesData) {
    const db::IndexDescriptor desc = MakeIndex("users_id_idx", "users", "id");

    {
        db::IndexManager manager(512U);
        db::Status status = manager.OpenIndex(desc, db::ValueType::kInt);
        EXPECT_FALSE(status.ok());
        EXPECT_EQ(status.code(), db::StatusCode::kNotFound);
    }

    {
        db::IndexManager manager(512U);
        ASSERT_TRUE(manager.CreateIndex(desc, db::ValueType::kInt).ok());
        ASSERT_TRUE(manager.Insert(desc.name, db::Value::Int(42), db::RowId{42}).ok());
    }

    {
        db::IndexManager manager(512U);
        ASSERT_TRUE(manager.OpenIndex(desc, db::ValueType::kInt).ok());

        std::optional<db::RowId> found;
        ASSERT_TRUE(manager.Find(desc.name, db::Value::Int(42), &found).ok());
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(found->value, 42U);
    }
}

TEST_F(IndexManagerTest, MultipleIndexesStayIndependent) {
    db::IndexManager manager(512U);
    const db::IndexDescriptor id_index = MakeIndex("users_id_idx", "users", "id");
    const db::IndexDescriptor active_index = MakeIndex("users_active_idx", "users", "active");

    ASSERT_TRUE(manager.CreateIndex(id_index, db::ValueType::kInt).ok());
    ASSERT_TRUE(manager.CreateIndex(active_index, db::ValueType::kBool).ok());

    ASSERT_TRUE(manager.Insert(id_index.name, db::Value::Int(7), db::RowId{7}).ok());
    ASSERT_TRUE(manager.Insert(active_index.name, db::Value::Bool(true), db::RowId{99}).ok());

    std::optional<db::RowId> found_id;
    ASSERT_TRUE(manager.Find(id_index.name, db::Value::Int(7), &found_id).ok());
    ASSERT_TRUE(found_id.has_value());
    EXPECT_EQ(found_id->value, 7U);

    std::optional<db::RowId> found_active;
    ASSERT_TRUE(manager.Find(active_index.name, db::Value::Bool(true), &found_active).ok());
    ASSERT_TRUE(found_active.has_value());
    EXPECT_EQ(found_active->value, 99U);
}

TEST_F(IndexManagerTest, OversizedStringKeysAreRejectedByConfiguredPolicy) {
    db::IndexManager manager(512U, 32U);
    const db::IndexDescriptor desc = MakeIndex("users_name_idx", "users", "name");

    ASSERT_TRUE(manager.CreateIndex(desc, db::ValueType::kString).ok());

    const std::string long_name(40U, 'x');
    const db::Status status = manager.Insert(desc.name, db::Value::String(long_name), db::RowId{1});
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

}  // namespace
