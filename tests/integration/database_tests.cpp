// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <gtest/gtest.h> // фреймворк для тестирования (код человека 1)
#include "server/database.h" // тестируемый класс (код человека 1)
#include "common/config.h" // конфигурация (код человека 1)
#include <filesystem> // работа с файловой системой (код человека 1)
#include <string> // стандартный строковый тип (код человека 1)

namespace db { // пространство имён базы данных

class DatabaseIntegrationTest : public ::testing::Test { // fixture для тестов Database
protected:
    std::filesystem::path temp_dir_; // временная директория для данных

    void SetUp() override { // подготовка перед каждым тестом (код человека 1)
        temp_dir_ = std::filesystem::temp_directory_path() / "coursedb_int_test"; // путь к temp (код человека 1)
        std::filesystem::remove_all(temp_dir_); // чистим перед тестом (код человека 1)
    } // SetUp

    void TearDown() override { // очистка после каждого теста (код человека 1)
        std::filesystem::remove_all(temp_dir_); // чистим после теста (код человека 1)
    } // TearDown

    Config MakeConfig() const { // фабрика конфига для тестов (код человека 1)
        Config cfg; // конфиг по умолчанию (код человека 1)
        cfg.data_dir = temp_dir_.string(); // указываем temp-директорию (код человека 1)
        return cfg; // возвращаем готовый конфиг (код человека 1)
    } // MakeConfig
}; // DatabaseIntegrationTest

// тест: жизненный цикл database (старт, доступ к engine-заглушке, стоп) (код человека 1)
TEST_F(DatabaseIntegrationTest, StartStopLifecycle) { // используем fixture
    Database db(MakeConfig()); // создаём database (код человека 1)
    EXPECT_TRUE(db.Start().ok()); // старт должен успешно завершиться (код человека 1)
    // TODO: engine будет доступен после интеграции с кодом человека 1 (код человека 1 ещё не предоставил реализацию)
    // auto& engine = db.engine(); // доступ к StorageNodeEngine (код человека 1)
    // (void)engine; // подавляем unused warning (код человека 1)
    EXPECT_TRUE(db.Stop().ok()); // стоп должен успешно завершиться (код человека 1)
} // StartStopLifecycle

// тест: перезапуск сохраняет data_dir (код человека 1)
TEST_F(DatabaseIntegrationTest, RestartPreservesDataDir) { // используем fixture
    { // первый запуск (код человека 1)
        Database db(MakeConfig()); // создаём database (код человека 1)
        EXPECT_TRUE(db.Start().ok()); // стартуем (код человека 1)
        EXPECT_TRUE(std::filesystem::exists(temp_dir_)); // директория данных создана (код человека 1)
        EXPECT_TRUE(db.Stop().ok()); // останавливаем (код человека 1)
    } // первый scope
    { // второй запуск (код человека 1)
        Database db(MakeConfig()); // создаём database снова (код человека 1)
        EXPECT_TRUE(db.Start().ok()); // стартуем повторно (код человека 1)
        EXPECT_TRUE(std::filesystem::exists(temp_dir_)); // директория данных на месте (код человека 1)
        EXPECT_TRUE(db.Stop().ok()); // останавливаем (код человека 1)
    } // второй scope
} // RestartPreservesDataDir

} // namespace db
