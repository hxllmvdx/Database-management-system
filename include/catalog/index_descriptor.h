#pragma once
#include <string>

namespace db {

struct IndexDescriptor {
    std::string name;
    std::string table_name;
    std::string column_name;
    std::string file_path;
    bool unique = true;
};

}
