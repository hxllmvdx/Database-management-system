#include <gtest/gtest.h>                              // фреймворк тестирования
#include <filesystem>                                 // для удаления старых тестовых директорий
#include "server/database.h"                          // объект базы данных
#include "server/query_processor.h"                   // процессор запросов
#include "server/session_context.h"                   // контекст сессии
#include "common/config.h"                            // настройки

using namespace db;                                   // используем пространство имён проекта

TEST(Database, Lifecycle) {                           // проверяем старт и стоп
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
    StorageNodeEngine& engine = db.engine();          // получаем ссылку на движок
    Status status = engine.CreateDatabase("testdb");  // создаём базу через движок
    ASSERT_TRUE(status.ok()) << status.message();     // операция прошла
    db.Stop();                                        // завершаем работу
}

TEST(Database, RestartPreservesData) {                // перезапуск бд и проверка сохранности данных
    Config config;                                    // настройки
    config.data_dir = "./test_data_restart";          // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым прогоном

    {                                                 // первый запуск
        Database db(config);                          // создаём базу
        ASSERT_TRUE(db.Start().ok());                 // стартуем
        QueryProcessor qp(&db);                       // процессор
        SessionContext ctx;                           // сессия
        ctx.client_id = "test";                       // фиктивный клиент
        QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
        ASSERT_TRUE(rc1.ok);
        QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
        ASSERT_TRUE(rc2.ok);
        QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // таблица
        ASSERT_TRUE(rc3.ok);
        QueryResult r = qp.Execute("INSERT INTO users (id, name) VALUE (42, \"persisted\")", &ctx); // данные
        ASSERT_TRUE(r.ok) << r.error;                 // вставка успешна
        ASSERT_EQ(r.affected_rows, 1u);               // затронута одна строка
        db.Stop();                                    // останавливаемся
    }

    Database db2(config);                             // второй запуск с той же директорией
    ASSERT_TRUE(db2.Start().ok());                    // стартуем заново
    QueryProcessor qp2(&db2);                         // новый процессор
    SessionContext ctx2;                              // новый контекст
    ctx2.client_id = "test";                          // тот же клиент
    QueryResult rc_use = qp2.Execute("USE testdb", &ctx2); // переключаемся на старую бд
    ASSERT_TRUE(rc_use.ok);
    QueryResult sel = qp2.Execute("SELECT * FROM users", &ctx2); // читаем данные
    ASSERT_TRUE(sel.ok) << sel.error;                 // select проходит
    ASSERT_EQ(sel.rows.size(), 1u);                   // строка на месте
    EXPECT_EQ(sel.rows[0].values[0].AsInt(), 42);     // id сохранился
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "persisted"); // name сохранился
    db2.Stop();                                       // остановка
}
