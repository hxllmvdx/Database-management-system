#include "server/database.h"
#include "common/config.h"
#include "common/file_utils.h"
#include "common/status.h"
#include "logging/logger.h"

namespace db {

Database::Database(Config config)
    : config_(std::move(config)),
      engine_(config_) {}

Status Database::Start() {
    Logger::Init(config_);
    Status status = file_utils::EnsureDir(config_.data_dir);
    if (!status.ok()) return status;
    Logger::Info("database starting", {{"data_dir", config_.data_dir}});
    return engine_.Start();
}

const Config& Database::config() const {
    return config_;
}

Status Database::Stop() {
    return engine_.Stop();
}

StorageNodeEngine& Database::engine() {
    return engine_;
}

} 
