// исходный код написан человеком 1, комментарии и заготовки добавлены человеком 3
#pragma once
#include <string> // стандартный строковый тип
#include "../server/query_result.h" // единый формат результата запроса
#include "../server/session_context.h" // состояние сессии (текущая бд, пользователь)

namespace db { // пространство имён базы данных

class Database; // forward declaration: Database владеет StorageNodeEngine (код человека 1)

class QueryProcessor { // обработчик sql-запросов: парсинг → планирование → выполнение
public:
    explicit QueryProcessor(Database* db); // принимает указатель на Database для доступа к engine

    QueryResult Execute(const std::string& sql,
                        SessionContext* session); // главная точка входа: sql + контекст → результат

private:
    Database* db_; // указатель на Database (код человека 1), доступ к StorageNodeEngine через db_->engine()
}; // внутри Execute три фазы: Parse → Plan → ExecutePlan (ЗАГОТОВКИ ДЛЯ ЧЕЛОВЕКА 2)

} // namespace db
