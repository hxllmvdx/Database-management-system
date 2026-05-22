#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#define private public
#include "storage/page_manager.h"
#undef private

namespace {

struct TestPageFileHeaderLayout {
    char magic[8];
    std::uint32_t version = 0;
    std::uint64_t page_size = 0;
    std::uint64_t page_count = 0;
};

constexpr std::uint64_t kPageFileHeaderSize =
    static_cast<std::uint64_t>(sizeof(TestPageFileHeaderLayout));

class PageManagerTest : public ::testing::Test {
protected:
    static constexpr std::size_t kPageSize = 256;

    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "coursedb_page_manager_tests";
        ASSERT_TRUE(std::filesystem::create_directories(test_dir_) || std::filesystem::exists(test_dir_));
        file_path_ = test_dir_ / (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".db");
        std::error_code ec;
        std::filesystem::remove(file_path_, ec);
        ASSERT_FALSE(std::filesystem::exists(file_path_));
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(file_path_, ec);
    }

    db::PageManager CreateManager() const {
        return db::PageManager(file_path_.string(), kPageSize);
    }

    static db::ByteBuffer MakePageData(std::size_t page_size, std::uint8_t seed) {
        db::ByteBuffer data(page_size);
        for (std::size_t i = 0; i < page_size; ++i) {
            data[i] = static_cast<db::Byte>((seed + static_cast<std::uint8_t>(i)) % 251U);
        }
        return data;
    }

    std::filesystem::path file_path_;
    std::filesystem::path test_dir_;
};

TEST_F(PageManagerTest, OpenCreatesNewFile) {
    db::PageManager manager = CreateManager();

    EXPECT_FALSE(std::filesystem::exists(file_path_));

    db::Status status = manager.Open();

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_TRUE(std::filesystem::exists(file_path_));
    EXPECT_EQ(std::filesystem::file_size(file_path_), kPageFileHeaderSize);
    EXPECT_EQ(manager.page_count_, 0U);
}

TEST_F(PageManagerTest, AllocatePageCreatesPage) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId page_id;
    db::Status status = manager.AllocatePage(&page_id);
    db::Status flush_status = manager.Flush();

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_TRUE(flush_status.ok()) << flush_status.message();
    EXPECT_EQ(page_id, db::PageId{0});
    EXPECT_EQ(std::filesystem::file_size(file_path_), kPageFileHeaderSize + kPageSize);
}

TEST_F(PageManagerTest, NewlyAllocatedPageReadsBackAsZeroed) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId page_id;
    ASSERT_TRUE(manager.AllocatePage(&page_id).ok());

    db::Page page;
    db::Status status = manager.ReadPage(page_id, &page);

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_EQ(page.id, page_id);
    ASSERT_EQ(page.data.size(), kPageSize);
    EXPECT_TRUE(std::all_of(page.data.begin(), page.data.end(), [](db::Byte byte) { return byte == 0; }));
}

TEST_F(PageManagerTest, WriteThenReadPagePreservesAllBytes) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId page_id;
    ASSERT_TRUE(manager.AllocatePage(&page_id).ok());

    db::Page written_page{page_id, MakePageData(kPageSize, 17U)};
    ASSERT_TRUE(manager.WritePage(written_page).ok());

    db::Page read_page;
    db::Status status = manager.ReadPage(page_id, &read_page);

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_EQ(read_page.id, page_id);
    EXPECT_EQ(read_page.data, written_page.data);
}

TEST_F(PageManagerTest, MultiplePagesKeepIndependentData) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    std::vector<std::pair<db::PageId, db::ByteBuffer>> pages;
    for (std::uint8_t seed : {3U, 71U, 149U}) {
        db::PageId page_id;
        ASSERT_TRUE(manager.AllocatePage(&page_id).ok());
        db::ByteBuffer data = MakePageData(kPageSize, seed);
        ASSERT_TRUE(manager.WritePage(db::Page{page_id, data}).ok());
        pages.emplace_back(page_id, std::move(data));
    }

    for (const auto& [page_id, expected_data] : pages) {
        db::Page page;
        db::Status status = manager.ReadPage(page_id, &page);
        ASSERT_TRUE(status.ok()) << status.message();
        EXPECT_EQ(page.data, expected_data);
    }
}

TEST_F(PageManagerTest, ReadMissingPageReturnsError) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId first_page;
    db::PageId second_page;
    ASSERT_TRUE(manager.AllocatePage(&first_page).ok());
    ASSERT_TRUE(manager.AllocatePage(&second_page).ok());

    db::Page page;
    db::Status status = manager.ReadPage(db::PageId{5}, &page);

    EXPECT_FALSE(status.ok());
}

TEST_F(PageManagerTest, WritePageWithWrongSizeReturnsError) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId page_id;
    ASSERT_TRUE(manager.AllocatePage(&page_id).ok());

    db::Page invalid_page{page_id, MakePageData(kPageSize - 1, 9U)};
    db::Status status = manager.WritePage(invalid_page);

    EXPECT_FALSE(status.ok());
}

TEST_F(PageManagerTest, OpenRejectsFileWithMisalignedSize) {
    {
        std::ofstream file(file_path_, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        std::vector<char> bytes(kPageFileHeaderSize + 1U, 0);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    db::PageManager manager = CreateManager();
    db::Status status = manager.Open();

    EXPECT_FALSE(status.ok());
}

TEST_F(PageManagerTest, OpenRejectsFileWithDifferentPageSizeInHeader) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());
    ASSERT_TRUE(manager.Flush().ok());

    db::PageManager mismatched_manager(file_path_.string(), kPageSize * 2U);
    db::Status status = mismatched_manager.Open();

    EXPECT_FALSE(status.ok());
}

TEST_F(PageManagerTest, FailedReadDoesNotBreakSubsequentValidRead) {
    db::PageManager manager = CreateManager();
    ASSERT_TRUE(manager.Open().ok());

    db::PageId page_id;
    ASSERT_TRUE(manager.AllocatePage(&page_id).ok());
    db::ByteBuffer expected = MakePageData(kPageSize, 33U);
    ASSERT_TRUE(manager.WritePage(db::Page{page_id, expected}).ok());

    db::Page missing_page;
    db::Status missing_status = manager.ReadPage(db::PageId{page_id.value + 10U}, &missing_page);
    EXPECT_FALSE(missing_status.ok());

    db::Page actual_page;
    db::Status actual_status = manager.ReadPage(page_id, &actual_page);

    ASSERT_TRUE(actual_status.ok()) << actual_status.message();
    EXPECT_EQ(actual_page.data, expected);
}

TEST_F(PageManagerTest, ReopenExistingFilePreservesAllocatedPagesAndData) {
    db::PageId first_page;
    db::PageId second_page;
    db::ByteBuffer first_data = MakePageData(kPageSize, 11U);
    db::ByteBuffer second_data = MakePageData(kPageSize, 87U);

    {
        db::PageManager manager = CreateManager();
        ASSERT_TRUE(manager.Open().ok());
        ASSERT_TRUE(manager.AllocatePage(&first_page).ok());
        ASSERT_TRUE(manager.AllocatePage(&second_page).ok());
        ASSERT_TRUE(manager.WritePage(db::Page{first_page, first_data}).ok());
        ASSERT_TRUE(manager.WritePage(db::Page{second_page, second_data}).ok());
        ASSERT_TRUE(manager.Flush().ok());
    }

    db::PageManager reopened_manager = CreateManager();
    db::Status open_status = reopened_manager.Open();

    ASSERT_TRUE(open_status.ok()) << open_status.message();
    EXPECT_EQ(reopened_manager.page_count_, 2U);

    db::Page first_read_page;
    db::Page second_read_page;
    ASSERT_TRUE(reopened_manager.ReadPage(first_page, &first_read_page).ok());
    ASSERT_TRUE(reopened_manager.ReadPage(second_page, &second_read_page).ok());
    EXPECT_EQ(first_read_page.data, first_data);
    EXPECT_EQ(second_read_page.data, second_data);
}

}  
