#pragma once
#include "../common/status.h"
#include "../catalog/table_descriptor.h"
#include "../parser/sql_statement.h"

namespace db {

class CatalogManager;

class Binder {
public:
    explicit Binder(CatalogManager* catalog);

    Status Bind(const std::string& current_db,
                const SqlStatement& stmt,
                TableDescriptor* bound_table);

private:
    CatalogManager* catalog_;
};

}
