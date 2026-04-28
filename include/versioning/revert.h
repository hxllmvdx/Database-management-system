#pragma once
#include <cstdint>
#include <string>
#include "../common/status.h"

namespace db {

class StorageNodeEngine;

class RevertService {
public:
    explicit RevertService(StorageNodeEngine* engine);

    Status RevertTableToTimestamp(const std::string& db_name,
                                  const std::string& table_name,
                                  std::int64_t timestamp_ms);

private:
    StorageNodeEngine* engine_;
};

}
