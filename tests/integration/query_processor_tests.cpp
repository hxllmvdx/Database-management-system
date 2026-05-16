#include <gtest/gtest.h>                              // фреймворк тестирования
#include <filesystem>                                 // удаление старых тестовых директорий
#include "server/database.h"                          // верхнеуровневая база
#include "server/query_processor.h"                   // исполнитель sql
#include "server/session_context.h"                   // состояние сессии
#include "common/config.h"                            // настройки

using namespace db;                                   // используем пространство имён проекта

TEST(QueryProcessor, CreateTableAndInsert) {          // базовый сквозной сценарий
    Config config;                                    // конфигурация по умолчанию
    config.data_dir = "./test_data_qp";               // изолируем директорию данных
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым прогоном
    Database db(config);                              // создаём объект базы
    ASSERT_TRUE(db.Start().ok());                     // запускаем движок

    QueryProcessor qp(&db);                           // процессор работает с этой базой
    SessionContext ctx;                               // контекст текущей сессии
    ctx.client_id = "test";                           // фиктивный идентификатор клиента

    QueryResult r1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём базу данных
    EXPECT_TRUE(r1.ok) << r1.error;                   // операция должна завершиться успешно

    QueryResult r2 = qp.Execute("USE testdb", &ctx);  // переключаем контекст на новую бд
    EXPECT_TRUE(r2.ok) << r2.error;

    QueryResult r3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // создаём таблицу
    EXPECT_TRUE(r3.ok) << r3.error;

    QueryResult r4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\")", &ctx); // вставляем строку
    EXPECT_TRUE(r4.ok) << r4.error;
    EXPECT_EQ(r4.affected_rows, 1u);                  // должна быть затронута ровно одна строка

    QueryResult r5 = qp.Execute("SELECT * FROM users", &ctx); // читаем всё обратно
    EXPECT_TRUE(r5.ok) << r5.error;
    ASSERT_EQ(r5.rows.size(), 1u);                    // ожидаем одну строку
    EXPECT_EQ(r5.rows[0].values[0].AsInt(), 1);       // проверяем id
    EXPECT_EQ(r5.rows[0].values[1].AsString(), "alice"); // проверяем name

    db.Stop();                                        // корректно останавливаемся
}

TEST(QueryProcessor, SyntaxErrorReturnsFailure) {     // обработка синтаксической ошибки
    Config config;                                    // локальная конфигурация
    config.data_dir = "./test_data_qp_err";           // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим мусор
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор запросов
    SessionContext ctx;                               // контекст
    ctx.client_id = "test";                           // фиктивный клиент

    QueryResult r = qp.Execute("INVALID SQL", &ctx);  // явно некорректный запрос
    EXPECT_FALSE(r.ok);                               // ожидаем признак ошибки

    db.Stop();                                        // завершаем работу
}

TEST(QueryProcessor, InsertManyAndSelectWhere) {      // вставка множества строк и выборка по условию
    Config config;                                    // настройки
    config.data_dir = "./test_data_qp_many";          // отдельная директория
    std::filesystem::remove_all(config.data_dir);     // чистим за прошлым запуском
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор
    SessionContext ctx;                               // сессия
    ctx.client_id = "test";                           // фиктивный id

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE nums (id INT, val STRING)", &ctx); // таблица
    ASSERT_TRUE(rc3.ok);

    for (int i = 1; i <= 100; ++i) {                  // вставляем сто строк
        std::string sql = "INSERT INTO nums (id, val) VALUE (" +
                          std::to_string(i) + ", \"v" + std::to_string(i) + "\")";
        QueryResult r = qp.Execute(sql, &ctx);        // выполняем insert
        ASSERT_TRUE(r.ok) << r.error;                 // каждый должен пройти
    }

    QueryResult r = qp.Execute("SELECT * FROM nums WHERE id > 50", &ctx); // выбираем часть
    EXPECT_TRUE(r.ok) << r.error;                     // запрос успешен
    EXPECT_EQ(r.rows.size(), 50u);                    // ожидаем ровно 50 строк
    EXPECT_EQ(r.rows[0].values[0].AsInt(), 51);       // первая должна быть с id=51

    db.Stop();                                        // останавливаем движок
}

TEST(QueryProcessor, DeleteThenSelectReturnsEmpty) {  // удаление строки и проверка отсутствия
    Config config;                                    // локальная конфигурация
    config.data_dir = "./test_data_qp_del";           // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор
    SessionContext ctx;                               // сессия
    ctx.client_id = "test";                           // фиктивный клиент

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // таблица
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\")", &ctx); // первая строка
    ASSERT_TRUE(rc4.ok);
    QueryResult rc5 = qp.Execute("INSERT INTO users (id, name) VALUE (2, \"bob\")", &ctx);   // вторая строка
    ASSERT_TRUE(rc5.ok);

    QueryResult del = qp.Execute("DELETE FROM users WHERE id == 1", &ctx); // удаляем по условию
    EXPECT_TRUE(del.ok) << del.error;                 // удаление успешно
    EXPECT_EQ(del.affected_rows, 1u);                 // затронута одна строка

    QueryResult sel = qp.Execute("SELECT * FROM users", &ctx); // выбираем оставшиеся
    EXPECT_TRUE(sel.ok) << sel.error;                 // select проходит
    ASSERT_EQ(sel.rows.size(), 1u);                   // осталась одна строка
    EXPECT_EQ(sel.rows[0].values[0].AsInt(), 2);      // id == 2
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "bob"); // name == bob

    db.Stop();                                        // остановка
}

TEST(QueryProcessor, UpdateThenSelectReturnsNewValue) { // обновление строки и проверка нового значения
    Config config;                                    // настройки
    config.data_dir = "./test_data_qp_upd";           // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор
    SessionContext ctx;                               // сессия
    ctx.client_id = "test";                           // фиктивный клиент

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // таблица
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\")", &ctx); // начальные данные
    ASSERT_TRUE(rc4.ok);

    QueryResult upd = qp.Execute("UPDATE users SET name = \"charlie\" WHERE id == 1", &ctx); // обновляем
    EXPECT_TRUE(upd.ok) << upd.error;                 // запрос успешен
    EXPECT_EQ(upd.affected_rows, 1u);                 // затронута одна строка

    QueryResult sel = qp.Execute("SELECT * FROM users", &ctx); // читаем результат
    EXPECT_TRUE(sel.ok) << sel.error;                 // select проходит
    ASSERT_EQ(sel.rows.size(), 1u);                   // одна строка
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "charlie"); // имя изменилось

    db.Stop();                                        // остановка
}

TEST(QueryProcessor, MultipleTablesSimultaneously) {  // работа с несколькими таблицами сразу
    Config config;                                    // настройки
    config.data_dir = "./test_data_qp_multi";         // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор
    SessionContext ctx;                               // сессия
    ctx.client_id = "test";                           // фиктивный клиент

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    ASSERT_TRUE(rc1.ok);
    QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE t1 (id INT, name STRING)", &ctx); // первая таблица
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("CREATE TABLE t2 (id INT, age INT)", &ctx);     // вторая таблица
    ASSERT_TRUE(rc4.ok);

    QueryResult rc5 = qp.Execute("INSERT INTO t1 (id, name) VALUE (1, \"alice\")", &ctx); // в t1
    ASSERT_TRUE(rc5.ok);
    QueryResult rc6 = qp.Execute("INSERT INTO t2 (id, age) VALUE (1, 25)", &ctx);          // в t2
    ASSERT_TRUE(rc6.ok);

    QueryResult r1 = qp.Execute("SELECT * FROM t1", &ctx); // читаем из первой
    EXPECT_TRUE(r1.ok) << r1.error;                   // успешно
    ASSERT_EQ(r1.rows.size(), 1u);                    // одна строка
    EXPECT_EQ(r1.rows[0].values[1].AsString(), "alice"); // проверяем данные t1

    QueryResult r2 = qp.Execute("SELECT * FROM t2", &ctx); // читаем из второй
    EXPECT_TRUE(r2.ok) << r2.error;                   // успешно
    ASSERT_EQ(r2.rows.size(), 1u);                    // одна строка
    EXPECT_EQ(r2.rows[0].values[1].AsInt(), 25);      // проверяем данные t2

    db.Stop();                                        // останавливаемся
}

TEST(QueryProcessor, ErrorDoesNotCorruptDatabaseState) { // ошибка в запросе не ломает состояние бд
    Config config;                                    // настройки
    config.data_dir = "./test_data_qp_err_state";     // изолированная папка
    std::filesystem::remove_all(config.data_dir);     // чистим
    Database db(config);                              // создаём базу
    ASSERT_TRUE(db.Start().ok());                     // стартуем

    QueryProcessor qp(&db);                           // процессор
    SessionContext ctx;                               // сессия
    ctx.client_id = "test";                           // фиктивный клиент

    QueryResult rc1 = qp.Execute("CREATE DATABASE testdb", &ctx); // создаём бд
    ASSERT_TRUE(rc1.ok);                              // проверяем успех
    QueryResult rc2 = qp.Execute("USE testdb", &ctx); // переключаемся
    ASSERT_TRUE(rc2.ok);
    QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING)", &ctx); // таблица
    ASSERT_TRUE(rc3.ok);
    QueryResult rc4 = qp.Execute("INSERT INTO users (id, name) VALUE (1, \"alice\")", &ctx); // данные
    ASSERT_TRUE(rc4.ok);

    QueryResult bad = qp.Execute("THIS IS NOT SQL", &ctx); // явно битый запрос
    EXPECT_FALSE(bad.ok);                             // ожидаем ошибку

    QueryResult sel = qp.Execute("SELECT * FROM users", &ctx); // следующий корректный запрос
    EXPECT_TRUE(sel.ok) << sel.error;                 // должен выполниться без проблем
    ASSERT_EQ(sel.rows.size(), 1u);                   // данные на месте
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "alice"); // значение не испорчено

    db.Stop();                                        // остановка
}
