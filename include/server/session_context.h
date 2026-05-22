#pragma once
#include <string>

namespace db {

struct SessionContext {
    std::string client_id;
    std::string current_db;
    std::string user_id;
    bool authenticated = false;
};

}
