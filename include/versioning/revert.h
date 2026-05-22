#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../common/status.h"
#include "version_record.h"

namespace db {

class StorageNodeEngine;

class RevertService {
public:
    explicit RevertService(StorageNodeEngine* engine);

    Status RevertTableToTimestamp(const std::string& db_name,
                                  const std::string& table_name,
                                  std::int64_t timestamp_ms);

private:
    Status LoadRecords(const std::string& db_name,
                       const std::string& table_name,
                       std::vector<VersionRecord>* out);
    Status CollectRollbackRecords(const std::string& table_name,
                                  const std::vector<VersionRecord>& all_records,
                                  std::int64_t timestamp_ms,
                                  std::vector<VersionRecord>* out);
    Status ApplyInverseRecord(const std::string& db_name,
                              const std::string& table_name,
                              const VersionRecord& record);

    StorageNodeEngine* engine_;
};

}
