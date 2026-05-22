#pragma once
#include <string>
#include "../server/query_result.h"

namespace db {

struct Response {
    bool ok = true;
    std::string error;
    QueryResult result;
};

}
