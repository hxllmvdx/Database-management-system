#pragma once
#include <string>
#include <vector>
#include "../common/status.h"
#include "table_descriptor.h"

namespace db {

class CatalogManager {
public:
    explicit CatalogManager(std::string root_dir);

    Status CreateDatabase(const std::string& db_name);
    Status DropDatabase(const std::string& db_name);

    Status CreateTable(const TableDescriptor& table);
    Status DropTable(const std::string& db_name, const std::string& table_name);

    Status GetTable(const std::string& db_name,
                    const std::string& table_name,
                    TableDescriptor* out);

    Status ListTables(const std::string& db_name,
                      std::vector<TableDescriptor>* out);

private:
    std::string root_dir_;
};

}
