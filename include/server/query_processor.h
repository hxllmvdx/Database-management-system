#pragma once
#include <string>
#include "../parser/parser.h"
#include "../planner/planner.h"
#include "../server/query_result.h"
#include "../server/session_context.h"

namespace db {

class Database;

class QueryProcessor {
public:
    explicit QueryProcessor(Database* db);

    QueryResult Execute(const std::string& sql,
                        SessionContext* session);

private:
    Database* db_;
};

}
