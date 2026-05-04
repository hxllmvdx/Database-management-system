#pragma once
#include <string>
#include <vector>
#include "schema.h"
#include "index_descriptor.h"

namespace db {

struct TableDescriptor {
    std::string database_name;
    std::string table_name;
    std::string data_file;
    std::string index_dir;
    TableSchema schema;
    std::vector<IndexDescriptor> indexes;
};

}
