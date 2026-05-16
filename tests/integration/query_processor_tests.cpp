#include <gtest/gtest.h>                              // gtest
#include <filesystem>                                 // удаление старых данных
#include "server/database.h"                          // база
#include "server/query_processor.h"                   // процессор запросов
#include "server/session_context.h"                   // контекст сессии
#include "common/config.h"                            // настройки

using namespace db;                                   // без префикса

TEST(QueryProcessor, CreateTableAndInsert) {          // сквозной сценарий
    Config config;
    config.data_dir = "./test_data_qp";               // изолируем файлы
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым прогоном
    Database db(config);
    ASSERT_TRUE(db.Start().ok());                     // стартуем базу

    QueryProcessor qp(&db);                           // процессор привязан к базе
    SessionContext ctx;
    ctx.client_id = "test";                           // фиктивная сессия

    QueryResult r1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    EXPECT_TRUE(r1.ok) << r1.error;                   // успешно

    QueryResult r2 = qp.Execute("USE testdb", &ctx);  // переключаем контекст
    EXPECT_TRUE(r2.ok) << r2.error;

    QueryResult r3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // таблица
    EXPECT_TRUE(r3.ok) << r3.error;

    QueryResult r4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\")", &ctx); // вставка
    EXPECT_TRUE(r4.ok) << r4.error;
    EXPECT_EQ(r4.affected_rows, 1u);                  // затронута одна строка

    QueryResult r5 = qp.Execute("SELECT * FROM users", &ctx); // выборка
    EXPECT_TRUE(r5.ok) << r5.error;
    ASSERT_EQ(r5.rows.size(), 1u);                    // одна строка в ответе
    EXPECT_EQ(r5.rows[0].values[0].AsInt(), 1);       // id = 1
    EXPECT_EQ(r5.rows[0].values[1].AsString(), "alice"); // name = alice

    db.Stop();                                        // останавливаем
}

TEST(QueryProcessor, SyntaxErrorReturnsFailure) {     // ошибка парсинга не падает
    Config config;
    config.data_dir = "./test_data_qp_err";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());

    QueryProcessor qp(&db);
    SessionContext ctx;
    ctx.client_id = "test";

    QueryResult r = qp.Execute("INVALID SQL", &ctx);  // явно битый запрос
    EXPECT_FALSE(r.ok);                               // ожидаем ошибку

    db.Stop();
}
