#pragma once
#include <string>

namespace db {

struct Session {
    std::string client_id;
    std::string remote_addr;
};

}
