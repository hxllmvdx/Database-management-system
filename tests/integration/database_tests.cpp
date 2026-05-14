#include <gtest/gtest.h>
#include "server/database.h"
#include "common/config.h"
#include <filesystem>
#include <string>

namespace db {

class DatabaseIntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "coursedb_int_test";
        std::filesystem::remove_all(temp_dir_); // чистим перед тестом
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_); // чистим после теста
    }

    Config MakeConfig() const {
        Config cfg;
        cfg.data_dir = temp_dir_.string();
        return cfg;
    }
};

// проверяем lifecycle: старт, доступ к engine, стоп
TEST_F(DatabaseIntegrationTest, StartStopLifecycle) {
    Database db(MakeConfig());
    EXPECT_TRUE(db.Start().ok());
    // engine должен быть доступен
    auto& engine = db.engine();
    (void)engine; // просто проверяем, что ссылка валидна
    EXPECT_TRUE(db.Stop().ok());
}

// проверяем, что перезапуск не ломает состояние (директория создаётся)
TEST_F(DatabaseIntegrationTest, RestartPreservesDataDir) {
    {
        Database db(MakeConfig());
        EXPECT_TRUE(db.Start().ok());
        EXPECT_TRUE(std::filesystem::exists(temp_dir_));
        EXPECT_TRUE(db.Stop().ok());
    }
    {
        Database db(MakeConfig());
        EXPECT_TRUE(db.Start().ok());
        EXPECT_TRUE(std::filesystem::exists(temp_dir_));
        EXPECT_TRUE(db.Stop().ok());
    }
}

} // namespace db
