#include "server/database.h"
#include "common/config.h"
#include <filesystem>

namespace db {

Database::Database(Config config)
    : config_(std::move(config)), engine_(config_) {} // передаём конфиг в движок

Status Database::Start() {
    // создаём директорию для данных, если её нет
    std::filesystem::create_directories(config_.data_dir);
    return engine_.Start(); // запускаем движок
}

Status Database::Stop() {
    return engine_.Stop(); // останавливаем движок
}

StorageNodeEngine& Database::engine() {
    return engine_; // возвращаем ссылку на движок
}

const Config& Database::config() const {
    return config_; // возвращаем конфиг
}

} // namespace db
