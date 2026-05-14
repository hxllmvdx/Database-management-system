#pragma once
#include <string> // стандартный строковый тип
#include "../server/query_result.h" // результат выполнения запроса

namespace db { // пространство имён базы данных

struct Response { // ответ сервера клиенту по tcp
    bool ok = true;           // флаг успеха: true = запрос выполнен, false = ошибка
    std::string error;        // описание ошибки, заполняется только если ok == false
    QueryResult result;       // данные результата (колонки, строки, affected_rows)
}; // структура сериализуется через Protocol в json и отправляется клиенту

} // namespace db
