#include "runtime/storage_node_engine.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "common/file_utils.h"

namespace {

std::int64_t NowTimestampMs() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

db::Status MapColumnTypeToValueType(db::ColumnType type, db::ValueType* out) {
    if (out == nullptr) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    switch (type) {
        case db::ColumnType::kInt:
            *out = db::ValueType::kInt;
            return db::Status::Ok();
        case db::ColumnType::kString:
            *out = db::ValueType::kString;
            return db::Status::Ok();
        case db::ColumnType::kBool:
            *out = db::ValueType::kBool;
            return db::Status::Ok();
    }

    return db::Status::Error(db::StatusCode::kInternalError, "Unsupported column type");
}

}  // namespace

namespace db {

StorageNodeEngine::StorageNodeEngine(Config config)
    : config_(std::move(config)),
      catalog_manager_(config_.data_dir),
      index_manager_(config_.page_size),
      started_(false) {}

CatalogManager& StorageNodeEngine::catalog() {
    return catalog_manager_;
}

IndexManager& StorageNodeEngine::index_manager() {
    return index_manager_;
}

std::string StorageNodeEngine::MakeTableCacheKey(const std::string& db_name,
                                                 const std::string& table_name) {
    return db_name + "::" + table_name;
}

std::string StorageNodeEngine::MakeVersionLogCacheKey(const std::string& db_name,
                                                      const std::string& table_name) {
    return db_name + "::" + table_name;
}

Status StorageNodeEngine::EnsureStarted() const {
    if (!started_) {
        return Status::Error(StatusCode::kInternalError, "StorageNodeEngine is not started");
    }
    return Status::Ok();
}

Status StorageNodeEngine::LoadTableDescriptor(const std::string& db_name,
                                              const std::string& table_name,
                                              TableDescriptor* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    return catalog_manager_.GetTable(db_name, table_name, out);
}

Status StorageNodeEngine::ResolveVersionLogPath(const TableDescriptor& descriptor,
                                                std::string* out_file_path) const {
    if (out_file_path == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_file_path must not be nullptr");
    }

    const std::filesystem::path path =
        std::filesystem::path(config_.data_dir) / descriptor.database_name / "versions" /
        (descriptor.table_name + ".vlog");
    *out_file_path = path.string();
    return Status::Ok();
}

Status StorageNodeEngine::OpenTable(const std::string& db_name,
                                    const std::string& table_name,
                                    OpenTableState** out_table) {
    if (out_table == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_table must not be nullptr");
    }

    const std::string cache_key = MakeTableCacheKey(db_name, table_name);
    auto it = opened_tables_.find(cache_key);
    if (it != opened_tables_.end()) {
        *out_table = &it->second;
        return Status::Ok();
    }

    TableDescriptor descriptor;
    Status status = LoadTableDescriptor(db_name, table_name, &descriptor);
    if (!status.ok()) {
        return status;
    }

    auto storage = std::make_unique<TableStorage>(descriptor, config_.page_size);
    status = storage->Open();
    if (!status.ok()) {
        return status;
    }

    OpenTableState state;
    state.descriptor = descriptor;
    state.storage = std::move(storage);

    auto [inserted_it, inserted] = opened_tables_.emplace(cache_key, std::move(state));
    (void)inserted;
    *out_table = &inserted_it->second;
    return Status::Ok();
}

Status StorageNodeEngine::OpenVersionLog(const std::string& db_name,
                                         const std::string& table_name,
                                         VersionLog** out_log) {
    if (out_log == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_log must not be nullptr");
    }

    const std::string cache_key = MakeVersionLogCacheKey(db_name, table_name);
    auto it = opened_version_logs_.find(cache_key);
    if (it != opened_version_logs_.end()) {
        *out_log = it->second.get();
        return Status::Ok();
    }

    TableDescriptor descriptor;
    Status status = LoadTableDescriptor(db_name, table_name, &descriptor);
    if (!status.ok()) {
        return status;
    }

    std::string log_path;
    status = ResolveVersionLogPath(descriptor, &log_path);
    if (!status.ok()) {
        return status;
    }

    auto log = std::make_unique<VersionLog>(log_path);
    status = log->Open();
    if (!status.ok()) {
        return status;
    }

    VersionLog* raw_log = log.get();
    opened_version_logs_.emplace(cache_key, std::move(log));
    *out_log = raw_log;
    return Status::Ok();
}

Status StorageNodeEngine::ExtractIndexKey(const TableDescriptor& descriptor,
                                          const IndexDescriptor& index,
                                          const Tuple& tuple,
                                          Value* out_key) const {
    if (out_key == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_key must not be nullptr");
    }

    const int column_index = descriptor.schema.FindColumn(index.column_name);
    if (column_index < 0) {
        return Status::Error(StatusCode::kInternalError, "Indexed column not found in schema");
    }

    const std::size_t tuple_index = static_cast<std::size_t>(column_index);
    if (tuple_index >= tuple.values.size()) {
        return Status::Error(StatusCode::kInternalError, "Tuple does not contain indexed column");
    }

    *out_key = tuple.values[tuple_index];
    return Status::Ok();
}

Status StorageNodeEngine::EnsureTableIndexesCreated(const TableDescriptor& descriptor) {
    if (!descriptor.indexes.empty()) {
        Status status = file_utils::EnsureDir(descriptor.index_dir);
        if (!status.ok()) {
            return status;
        }
    }

    for (const IndexDescriptor& index : descriptor.indexes) {
        const ColumnSchema* column = descriptor.schema.FindColumnSchema(index.column_name);
        if (column == nullptr) {
            return Status::Error(StatusCode::kInternalError, "Indexed column not found in schema");
        }

        ValueType key_type = ValueType::kNull;
        Status status = MapColumnTypeToValueType(column->type, &key_type);
        if (!status.ok()) {
            return status;
        }

        status = index_manager_.CreateIndex(index, key_type);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status StorageNodeEngine::OpenTableIndexes(const TableDescriptor& descriptor) {
    for (const IndexDescriptor& index : descriptor.indexes) {
        const ColumnSchema* column = descriptor.schema.FindColumnSchema(index.column_name);
        if (column == nullptr) {
            return Status::Error(StatusCode::kInternalError, "Indexed column not found in schema");
        }

        ValueType key_type = ValueType::kNull;
        Status status = MapColumnTypeToValueType(column->type, &key_type);
        if (!status.ok()) {
            return status;
        }

        status = index_manager_.OpenIndex(index, key_type);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status StorageNodeEngine::InsertIntoIndexes(const TableDescriptor& descriptor,
                                            const Tuple& tuple,
                                            RowId rid) {
    for (const IndexDescriptor& index : descriptor.indexes) {
        Value key;
        Status status = ExtractIndexKey(descriptor, index, tuple, &key);
        if (!status.ok()) {
            return status;
        }
        if (key.is_null()) {
            continue;
        }

        status = index_manager_.Insert(index.name, key, rid);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status StorageNodeEngine::DeleteFromIndexes(const TableDescriptor& descriptor, const Tuple& tuple) {
    for (const IndexDescriptor& index : descriptor.indexes) {
        Value key;
        Status status = ExtractIndexKey(descriptor, index, tuple, &key);
        if (!status.ok()) {
            return status;
        }
        if (key.is_null()) {
            continue;
        }

        status = index_manager_.Delete(index.name, key);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status StorageNodeEngine::UpdateIndexes(const TableDescriptor& descriptor,
                                        const Tuple& before,
                                        const Tuple& after,
                                        RowId rid) {
    for (const IndexDescriptor& index : descriptor.indexes) {
        Value old_key;
        Status status = ExtractIndexKey(descriptor, index, before, &old_key);
        if (!status.ok()) {
            return status;
        }

        Value new_key;
        status = ExtractIndexKey(descriptor, index, after, &new_key);
        if (!status.ok()) {
            return status;
        }

        if (old_key.is_null() && new_key.is_null()) {
            continue;
        }
        if (old_key.is_null() && !new_key.is_null()) {
            status = index_manager_.Insert(index.name, new_key, rid);
            if (!status.ok()) {
                return status;
            }
            continue;
        }
        if (!old_key.is_null() && new_key.is_null()) {
            status = index_manager_.Delete(index.name, old_key);
            if (!status.ok()) {
                return status;
            }
            continue;
        }
        if (old_key == new_key) {
            continue;
        }

        status = index_manager_.Delete(index.name, old_key);
        if (!status.ok()) {
            return status;
        }
        status = index_manager_.Insert(index.name, new_key, rid);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status StorageNodeEngine::AppendInsertVersion(const std::string& db_name,
                                              const std::string& table_name,
                                              RowId rid,
                                              const Tuple& after) {
    VersionLog* log = nullptr;
    Status status = OpenVersionLog(db_name, table_name, &log);
    if (!status.ok()) {
        return status;
    }

    VersionRecord record;
    record.timestamp_ms = NowTimestampMs();
    record.table_name = table_name;
    record.rid = rid;
    record.op = VersionOp::kInsert;
    record.after = after;
    status = log->Append(record);
    if (!status.ok()) {
        return status;
    }
    return log->Flush();
}

Status StorageNodeEngine::AppendUpdateVersion(const std::string& db_name,
                                              const std::string& table_name,
                                              RowId rid,
                                              const Tuple& before,
                                              const Tuple& after) {
    VersionLog* log = nullptr;
    Status status = OpenVersionLog(db_name, table_name, &log);
    if (!status.ok()) {
        return status;
    }

    VersionRecord record;
    record.timestamp_ms = NowTimestampMs();
    record.table_name = table_name;
    record.rid = rid;
    record.op = VersionOp::kUpdate;
    record.before = before;
    record.after = after;
    status = log->Append(record);
    if (!status.ok()) {
        return status;
    }
    return log->Flush();
}

Status StorageNodeEngine::AppendDeleteVersion(const std::string& db_name,
                                              const std::string& table_name,
                                              RowId rid,
                                              const Tuple& before) {
    VersionLog* log = nullptr;
    Status status = OpenVersionLog(db_name, table_name, &log);
    if (!status.ok()) {
        return status;
    }

    VersionRecord record;
    record.timestamp_ms = NowTimestampMs();
    record.table_name = table_name;
    record.rid = rid;
    record.op = VersionOp::kDelete;
    record.before = before;
    status = log->Append(record);
    if (!status.ok()) {
        return status;
    }
    return log->Flush();
}

Status StorageNodeEngine::Start() {
    if (started_) {
        return Status::Ok();
    }
    started_ = true;
    return Status::Ok();
}

Status StorageNodeEngine::Stop() {
    opened_tables_.clear();
    opened_version_logs_.clear();
    started_ = false;
    return Status::Ok();
}

Status StorageNodeEngine::CreateDatabase(const std::string& db_name) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }
    return catalog_manager_.CreateDatabase(db_name);
}

Status StorageNodeEngine::DropDatabase(const std::string& db_name) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    const std::string prefix = db_name + "::";
    for (auto it = opened_tables_.begin(); it != opened_tables_.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = opened_tables_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = opened_version_logs_.begin(); it != opened_version_logs_.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = opened_version_logs_.erase(it);
        } else {
            ++it;
        }
    }

    return catalog_manager_.DropDatabase(db_name);
}

Status StorageNodeEngine::CreateTable(const TableDescriptor& desc) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    status = catalog_manager_.CreateTable(desc);
    if (!status.ok()) {
        return status;
    }

    TableDescriptor normalized_desc;
    status = LoadTableDescriptor(desc.database_name, desc.table_name, &normalized_desc);
    if (!status.ok()) {
        return status;
    }

    return EnsureTableIndexesCreated(normalized_desc);
}

Status StorageNodeEngine::DropTable(const std::string& db_name, const std::string& table_name) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    opened_tables_.erase(MakeTableCacheKey(db_name, table_name));
    opened_version_logs_.erase(MakeVersionLogCacheKey(db_name, table_name));
    return catalog_manager_.DropTable(db_name, table_name);
}

Status StorageNodeEngine::GetTableDescriptor(const std::string& db_name,
                                             const std::string& table_name,
                                             TableDescriptor* out) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }
    return LoadTableDescriptor(db_name, table_name, out);
}

Status StorageNodeEngine::ScanTable(const std::string& db_name,
                                    const std::string& table_name,
                                    std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    OpenTableState* table_state = nullptr;
    status = OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }

    return table_state->storage->Scan(out);
}

Status StorageNodeEngine::InsertInternal(const std::string& db_name,
                                         const std::string& table_name,
                                         const Tuple& tuple,
                                         RowId* out_rid,
                                         bool write_version_record) {
    if (out_rid == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_rid must not be nullptr");
    }

    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    OpenTableState* table_state = nullptr;
    status = OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }

    status = OpenTableIndexes(table_state->descriptor);
    if (!status.ok()) {
        return status;
    }

    status = table_state->storage->Insert(tuple, out_rid);
    if (!status.ok()) {
        return status;
    }

    Row inserted_row;
    status = table_state->storage->Get(*out_rid, &inserted_row);
    if (!status.ok()) {
        return status;
    }

    status = InsertIntoIndexes(table_state->descriptor, inserted_row.tuple, *out_rid);
    if (!status.ok()) {
        return status;
    }

    if (!write_version_record) {
        return Status::Ok();
    }

    return AppendInsertVersion(db_name, table_name, *out_rid, inserted_row.tuple);
}

Status StorageNodeEngine::UpdateInternal(const std::string& db_name,
                                         const std::string& table_name,
                                         RowId rid,
                                         const Tuple& tuple,
                                         bool write_version_record) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    OpenTableState* table_state = nullptr;
    status = OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }

    status = OpenTableIndexes(table_state->descriptor);
    if (!status.ok()) {
        return status;
    }

    Row before_row;
    status = table_state->storage->Get(rid, &before_row);
    if (!status.ok()) {
        return status;
    }

    status = table_state->storage->Update(rid, tuple);
    if (!status.ok()) {
        return status;
    }

    Row after_row;
    status = table_state->storage->Get(rid, &after_row);
    if (!status.ok()) {
        return status;
    }

    status = UpdateIndexes(table_state->descriptor, before_row.tuple, after_row.tuple, rid);
    if (!status.ok()) {
        return status;
    }

    if (!write_version_record) {
        return Status::Ok();
    }

    return AppendUpdateVersion(db_name, table_name, rid, before_row.tuple, after_row.tuple);
}

Status StorageNodeEngine::DeleteInternal(const std::string& db_name,
                                         const std::string& table_name,
                                         RowId rid,
                                         bool write_version_record) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    OpenTableState* table_state = nullptr;
    status = OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }

    status = OpenTableIndexes(table_state->descriptor);
    if (!status.ok()) {
        return status;
    }

    Row before_row;
    status = table_state->storage->Get(rid, &before_row);
    if (!status.ok()) {
        return status;
    }

    status = DeleteFromIndexes(table_state->descriptor, before_row.tuple);
    if (!status.ok()) {
        return status;
    }

    status = table_state->storage->Delete(rid);
    if (!status.ok()) {
        return status;
    }

    if (!write_version_record) {
        return Status::Ok();
    }

    return AppendDeleteVersion(db_name, table_name, rid, before_row.tuple);
}

Status StorageNodeEngine::RestoreInternal(const std::string& db_name,
                                          const std::string& table_name,
                                          RowId rid,
                                          const Tuple& tuple,
                                          bool write_version_record) {
    Status status = EnsureStarted();
    if (!status.ok()) {
        return status;
    }

    OpenTableState* table_state = nullptr;
    status = OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }

    status = OpenTableIndexes(table_state->descriptor);
    if (!status.ok()) {
        return status;
    }

    Row existing_row;
    status = table_state->storage->Get(rid, &existing_row);
    if (status.ok()) {
        return Status::Error(StatusCode::kAlreadyExists, "Row already exists");
    }
    if (status.code() != StatusCode::kNotFound) {
        return status;
    }

    status = table_state->storage->Restore(rid, tuple);
    if (!status.ok()) {
        return status;
    }

    Row restored_row;
    status = table_state->storage->Get(rid, &restored_row);
    if (!status.ok()) {
        return status;
    }

    status = InsertIntoIndexes(table_state->descriptor, restored_row.tuple, rid);
    if (!status.ok()) {
        return status;
    }

    if (!write_version_record) {
        return Status::Ok();
    }

    return AppendInsertVersion(db_name, table_name, rid, restored_row.tuple);
}

Status StorageNodeEngine::Insert(const std::string& db_name,
                                 const std::string& table_name,
                                 const Tuple& tuple,
                                 RowId* out_rid) {
    return InsertInternal(db_name, table_name, tuple, out_rid, true);
}

Status StorageNodeEngine::Update(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid,
                                 const Tuple& tuple) {
    return UpdateInternal(db_name, table_name, rid, tuple, true);
}

Status StorageNodeEngine::Delete(const std::string& db_name,
                                 const std::string& table_name,
                                 RowId rid) {
    return DeleteInternal(db_name, table_name, rid, true);
}

}  // namespace db
