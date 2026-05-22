#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/status.h"
#include "../common/config.h"
#include "../catalog/catalog_manager.h"
#include "../index/index_manager.h"
#include "../storage/table_storage.h"
#include "../versioning/version_log.h"

namespace db {

class SqlStatement;
struct QueryResult;
class RevertService;

class StorageNodeEngine {
public:
    explicit StorageNodeEngine(Config config);

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
    friend class RevertService;

    struct OpenTableState {
        TableDescriptor descriptor;
        std::unique_ptr<TableStorage> storage;
    };

    static std::string MakeTableCacheKey(const std::string& db_name, const std::string& table_name);
    static std::string MakeVersionLogCacheKey(const std::string& db_name, const std::string& table_name);

    Status EnsureStarted() const;
    Status LoadTableDescriptor(const std::string& db_name,
                               const std::string& table_name,
                               TableDescriptor* out);
    Status OpenTable(const std::string& db_name,
                     const std::string& table_name,
                     OpenTableState** out_table);
    Status OpenVersionLog(const std::string& db_name,
                          const std::string& table_name,
                          VersionLog** out_log);
    Status OpenTableIndexes(const TableDescriptor& descriptor);
    Status EnsureTableIndexesCreated(const TableDescriptor& descriptor);
    Status ExtractIndexKey(const TableDescriptor& descriptor,
                           const IndexDescriptor& index,
                           const Tuple& tuple,
                           Value* out_key) const;
    Status InsertIntoIndexes(const TableDescriptor& descriptor, const Tuple& tuple, RowId rid);
    Status DeleteFromIndexes(const TableDescriptor& descriptor, const Tuple& tuple);
    Status UpdateIndexes(const TableDescriptor& descriptor,
                         const Tuple& before,
                         const Tuple& after,
                         RowId rid);
    Status InsertInternal(const std::string& db_name,
                          const std::string& table_name,
                          const Tuple& tuple,
                          RowId* out_rid,
                          bool write_version_record);
    Status UpdateInternal(const std::string& db_name,
                          const std::string& table_name,
                          RowId rid,
                          const Tuple& tuple,
                          bool write_version_record);
    Status DeleteInternal(const std::string& db_name,
                          const std::string& table_name,
                          RowId rid,
                          bool write_version_record);
    Status RestoreInternal(const std::string& db_name,
                           const std::string& table_name,
                           RowId rid,
                           const Tuple& tuple,
                           bool write_version_record);
    Status AppendInsertVersion(const std::string& db_name,
                               const std::string& table_name,
                               RowId rid,
                               const Tuple& after);
    Status AppendUpdateVersion(const std::string& db_name,
                               const std::string& table_name,
                               RowId rid,
                               const Tuple& before,
                               const Tuple& after);
    Status AppendDeleteVersion(const std::string& db_name,
                               const std::string& table_name,
                               RowId rid,
                               const Tuple& before);
    Status ResolveVersionLogPath(const TableDescriptor& descriptor, std::string* out_file_path) const;

    Config config_;
    CatalogManager catalog_manager_;
    IndexManager index_manager_;
    bool started_ = false;
    std::unordered_map<std::string, OpenTableState> opened_tables_;
    std::unordered_map<std::string, std::unique_ptr<VersionLog>> opened_version_logs_;
};

}
