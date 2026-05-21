#include "server/database.h"
#include "common/config.h"
#include "common/file_utils.h"
#include "common/status.h"

namespace db {

Database::Database(Config config)
    : config_(std::move(config)),
      engine_(config_) {}

Status Database::Start() {
    Status status = file_utils::EnsureDir(config_.data_dir);
    if (!status.ok()) return status;
    return engine_.Start();
}

Status Database::Stop() {
    return engine_.Stop();
}

StorageNodeEngine& Database::engine() {
    return engine_;
}

} // namespace db
