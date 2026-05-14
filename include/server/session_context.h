// исходный код написан человеком 1, комментарии добавлены человеком 3
#pragma once
#include <string> // стандартный строковый тип

namespace db { // пространство имён базы данных

struct SessionContext { // состояние sql-сессии клиента, живёт между запросами
    std::string client_id;     // идентификатор клиента, связывает с Session
    std::string current_db;    // имя текущей выбранной базы данных (команда USE)
    std::string user_id;       // идентификатор аутентифицированного пользователя
    bool authenticated = false; // флаг успешной аутентификации
}; // хранится в StorageService в глобальном map по client_id

} // namespace db
