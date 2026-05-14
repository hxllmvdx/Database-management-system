// исходный код написан человеком 1, комментарии добавлены человеком 3
#pragma once
#include <string>  // стандартный строковый тип
#include <vector>  // динамический массив
#include "../common/tuple.h" // строка результата запроса (Value + Tuple)

namespace db { // пространство имён базы данных

struct QueryResult { // единый формат результата выполнения любого sql-запроса
    bool ok = true;                    // флаг успеха операции
    std::string error;                 // текст ошибки, заполняется если ok == false
    std::vector<std::string> columns;  // имена колонок для SELECT, пусто для DDL/DML
    std::vector<Tuple> rows;           // строки результата SELECT, пусто для DDL/DML
    std::size_t affected_rows = 0;     // количество затронутых строк для INSERT/UPDATE/DELETE
}; // передаётся из QueryProcessor в StorageService и упаковывается в Response

// преобразует QueryResult в json-строку для вывода в терминал клиента
std::string QueryResultToJson(const QueryResult& result);

} // namespace db
