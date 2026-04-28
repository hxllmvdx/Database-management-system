#pragma once
#include <string>

namespace db {

struct IndexDescriptor {
    std::string name;
    std::string table_name;
    std::string column_name;
    bool unique = true;
};

}
