#pragma once
#include <string> // стандартный строковый тип

namespace db { // пространство имён базы данных

struct Request { // запрос от клиента к серверу по tcp
    std::string request_id; // уникальный идентификатор запроса для логирования
    std::string database;   // имя целевой базы данных (переопределяет контекст сессии)
    std::string sql;        // текст sql-запроса, завершается символом ;
    std::string auth_token; // jwt-токен для авторизации, пустой если не аутентифицирован
}; // структура передаётся через Protocol как json

} // namespace db
