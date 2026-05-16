#include <gtest/gtest.h>                              // gtest
#include <filesystem>                                 // для удаления старых тестовых директорий
#include "server/database.h"                          // объект базы данных
#include "common/config.h"                            // настройки

using namespace db;                                   // без префикса

TEST(Database, Lifecycle) {                           // проверяем start/stop
    Config config;                                    // дефолтная конфигурация
    config.data_dir = "./test_data_lifecycle";        // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым запуском
    Database db(config);                              // создаём объект
    Status status = db.Start();                       // запускаем
    ASSERT_TRUE(status.ok()) << status.message();     // старт успешен
    status = db.Stop();                               // останавливаем
    ASSERT_TRUE(status.ok()) << status.message();     // стоп успешен
}

TEST(Database, EngineAccess) {                        // проверяем доступ к движку
    Config config;
    config.data_dir = "./test_data_engine";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());                     // стартуем
    StorageNodeEngine& engine = db.engine();          // получаем ссылку
    Status status = engine.CreateDatabase("testdb");  // создаём базу через движок
    ASSERT_TRUE(status.ok()) << status.message();     // операция прошла
    db.Stop();                                        // завершаем работу
}
