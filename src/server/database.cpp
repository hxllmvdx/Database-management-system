// исходный код написан человеком 1, комментарии добавлены человеком 3
#include "server/database.h" // публичный интерфейс Database
#include "common/config.h"   // структура Config (data_dir, host, port)
#include <filesystem>        // создание директорий для данных

namespace db { // пространство имён базы данных

Database::Database(Config config)
    : config_(std::move(config)) {} // сохраняем конфиг, engine пока не создаём (TODO человек 1)

Status Database::Start() {
    std::filesystem::create_directories(config_.data_dir); // создаём директорию данных если нет
    // TODO (человек 1): инициализировать engine_ = new StorageNodeEngine(config_); engine_->Start();
    return Status::Ok(); // пока считаем что старт всегда успешен
} // Start

Status Database::Stop() {
    // TODO (человек 1): if (engine_) { engine_->Stop(); delete engine_; engine_ = nullptr; }
    return Status::Ok(); // пока считаем что стоп всегда успешен
} // Stop

StorageNodeEngine& Database::engine() {
    // TODO (человек 1): return *engine_; — после интеграции вернуть реальный engine
    static StorageNodeEngine* stub = nullptr; // заглушка для компиляции (forward declaration)
    return *stub; // НЕБЕЗОПАСНО: только до интеграции с человеком 1
} // engine

const Config& Database::config() const {
    return config_; // возвращаем ссылку на сохранённую конфигурацию
} // config

} // namespace db
