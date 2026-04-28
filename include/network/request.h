#pragma once
#include <string>

namespace db {

struct Request {
    std::string request_id;
    std::string database;
    std::string sql;
    std::string auth_token;
};

}
