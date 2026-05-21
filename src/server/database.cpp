#include "server/database.h"

#include <utility>

db::Database::Database(Config config) : engine_(std::move(config)) {}

db::Status db::Database::Start() {
    return engine_.Start();
}

db::Status db::Database::Stop() {
    return engine_.Stop();
}

db::StorageNodeEngine& db::Database::engine() {
    return engine_;
}
