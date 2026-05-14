#pragma once
#include <memory>
#include <string>
#include "../common/status.h"
#include "../common/config.h"
#include "../catalog/catalog_manager.h"
#include "../index/index_manager.h"
#include "../storage/table_storage.h"
#include "../versioning/version_log.h"

namespace db {

class SqlStatement;
struct QueryResult;
class TableStorage;

class StorageNodeEngine {
public:
    explicit StorageNodeEngine(Config config);
    ~StorageNodeEngine();

    Status Start();
    Status Stop();

    Status CreateDatabase(const std::string& db_name);
    Status DropDatabase(const std::string& db_name);

    Status CreateTable(const TableDescriptor& desc);
    Status DropTable(const std::string& db_name, const std::string& table_name);

    Status Insert(const std::string& db_name,
                  const std::string& table_name,
                  const Tuple& tuple,
                  RowId* out_rid);

    Status Update(const std::string& db_name,
                  const std::string& table_name,
                  RowId rid,
                  const Tuple& tuple);

    Status Delete(const std::string& db_name,
                  const std::string& table_name,
                  RowId rid);

    Status ScanTable(const std::string& db_name,
                     const std::string& table_name,
                     std::vector<Row>* out);

    Status GetTableDescriptor(const std::string& db_name,
                              const std::string& table_name,
                              TableDescriptor* out);

    CatalogManager& catalog();
    IndexManager& index_manager();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    TableStorage* GetTableStorage(const std::string& db_name,
                                  const std::string& table_name);
};

}
