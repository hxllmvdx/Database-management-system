#pragma once
#include <string>
#include <vector>
#include "../execution/tuple.h"
#include "../storage/row_id.h"

namespace db {

struct QueryResult {
    bool ok = true;
    std::string error;
    std::vector<std::string> columns;
    std::vector<Tuple> rows;
    std::size_t affected_rows = 0;
};

// преобразует результат запроса в json-строку для вывода в терминал
std::string QueryResultToJson(const QueryResult& result);

}
