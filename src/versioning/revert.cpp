#include "versioning/revert.h"

#include <utility>

#include "runtime/storage_node_engine.h"

namespace db {

RevertService::RevertService(StorageNodeEngine* engine) : engine_(engine) {}

Status RevertService::RevertTableToTimestamp(const std::string& db_name,
                                             const std::string& table_name,
                                             std::int64_t timestamp_ms) {
    if (engine_ == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "engine must not be nullptr");
    }

    std::vector<VersionRecord> all_records;
    Status status = LoadRecords(db_name, table_name, &all_records);
    if (!status.ok()) {
        return status;
    }

    std::vector<VersionRecord> rollback_records;
    status = CollectRollbackRecords(table_name, all_records, timestamp_ms, &rollback_records);
    if (!status.ok()) {
        return status;
    }

    for (auto it = rollback_records.rbegin(); it != rollback_records.rend(); ++it) {
        status = ApplyInverseRecord(db_name, table_name, *it);
        if (!status.ok()) {
            return status;
        }
    }

    return Status::Ok();
}

Status RevertService::LoadRecords(const std::string& db_name,
                                  const std::string& table_name,
                                  std::vector<VersionRecord>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    StorageNodeEngine::OpenTableState* table_state = nullptr;
    Status status = engine_->OpenTable(db_name, table_name, &table_state);
    if (!status.ok()) {
        return status;
    }
    (void)table_state;

    VersionLog* log = nullptr;
    status = engine_->OpenVersionLog(db_name, table_name, &log);
    if (!status.ok()) {
        return status;
    }

    return log->ReadAll(out);
}

Status RevertService::CollectRollbackRecords(const std::string& table_name,
                                             const std::vector<VersionRecord>& all_records,
                                             std::int64_t timestamp_ms,
                                             std::vector<VersionRecord>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    out->clear();
    for (const VersionRecord& record : all_records) {
        if (record.table_name != table_name) {
            continue;
        }
        if (record.timestamp_ms > timestamp_ms) {
            out->push_back(record);
        }
    }

    return Status::Ok();
}

Status RevertService::ApplyInverseRecord(const std::string& db_name,
                                         const std::string& table_name,
                                         const VersionRecord& record) {
    switch (record.op) {
        case VersionOp::kInsert:
            return engine_->DeleteInternal(db_name, table_name, record.rid, false);
        case VersionOp::kDelete:
            return engine_->RestoreInternal(db_name, table_name, record.rid, record.before, false);
        case VersionOp::kUpdate:
            return engine_->UpdateInternal(db_name, table_name, record.rid, record.before, false);
    }

    return Status::Error(StatusCode::kInvalidArgument, "Unsupported version operation");
}

}  
