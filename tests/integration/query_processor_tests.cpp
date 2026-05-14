// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <gtest/gtest.h> // фреймворк для тестирования (код человека 1)
#include "server/database.h" // композиционный объект (код человека 1)
#include "server/query_processor.h" // обработчик sql (код человека 1)
#include "server/session_context.h" // состояние сессии (код человека 1)
#include "common/config.h" // конфигурация (код человека 1)
#include <filesystem> // работа с файловой системой (код человека 1)

namespace db { // пространство имён базы данных

class QueryProcessorIntegrationTest : public ::testing::Test { // fixture для тестов QueryProcessor
protected:
    std::filesystem::path temp_dir_; // временная директория для данных
    std::unique_ptr<Database> db_; // указатель на database (код человека 1)
    std::unique_ptr<QueryProcessor> qp_; // указатель на query processor (код человека 1)
    SessionContext session_; // контекст тестовой сессии (код человека 1)

    void SetUp() override { // подготовка перед каждым тестом (код человека 1)
        temp_dir_ = std::filesystem::temp_directory_path() / "coursedb_qp_test"; // путь к temp (код человека 1)
        std::filesystem::remove_all(temp_dir_); // чистим старые данные (код человека 1)

        Config cfg; // конфиг по умолчанию (код человека 1)
        cfg.data_dir = temp_dir_.string(); // указываем temp-директорию (код человека 1)
        db_ = std::make_unique<Database>(cfg); // создаём database (код человека 1)
        ASSERT_TRUE(db_->Start().ok()); // стартуем, проверяем успех (код человека 1)
        qp_ = std::make_unique<QueryProcessor>(db_.get()); // создаём query processor (код человека 1)
        session_.client_id = "test_client"; // задаём id тестового клиента (код человека 1)
    } // SetUp

    void TearDown() override { // очистка после каждого теста (код человека 1)
        qp_.reset(); // уничтожаем query processor (код человека 1)
        if (db_) db_->Stop(); // останавливаем database если он есть (код человека 1)
        db_.reset(); // уничтожаем database (код человека 1)
        std::filesystem::remove_all(temp_dir_); // удаляем temp-директорию (код человека 1)
    } // TearDown
}; // QueryProcessorIntegrationTest

// тест: USE database обрабатывается на уровне QueryProcessor (код человека 1)
TEST_F(QueryProcessorIntegrationTest, UseDatabase) { // используем fixture
    auto r = qp_->Execute("USE testdb;", &session_); // выполняем USE (код человека 1)
    EXPECT_TRUE(r.ok); // ожидаем успех (код человека 1)
    EXPECT_EQ(session_.current_db, "testdb"); // проверяем что бд сохранилась в контексте (код человека 1)
} // UseDatabase

// тест: pipeline возвращает ошибку парсера для неподдерживаемых запросов (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
TEST_F(QueryProcessorIntegrationTest, UnsupportedQueryReturnsError) { // используем fixture
    auto r = qp_->Execute("CREATE DATABASE testdb;", &session_); // пытаемся выполнить DDL (код человека 1)
    EXPECT_FALSE(r.ok); // ожидаем ошибку, так как парсер — заглушка (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    EXPECT_NE(r.error.find("parser not implemented"), std::string::npos); // проверяем сообщение заглушки (код человека 1)
} // UnsupportedQueryReturnsError

// TODO (человек 2): после реализации parser/planner/execution добавить полные end-to-end тесты:
// - CREATE DATABASE / CREATE TABLE
// - INSERT / SELECT / DELETE
// - персистентность после рестарта

} // namespace db
