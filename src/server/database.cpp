#include "server/database.h"                            // интерфейс верхнеуровневой базы
#include "common/config.h"                              // конфигурация
#include "common/file_utils.h"                          // утилиты для директорий
#include "common/status.h"                              // статус-обёртка

namespace db {

Database::Database(Config config)                       // конструктор сохраняет конфиг
    : config_(std::move(config)),                       // копируем настройки
      engine_(config_) {}                               // передаём движку

Status Database::Start() {                              // запуск жизненного цикла
    Status status = file_utils::EnsureDir(config_.data_dir); // гарантируем директорию данных
    if (!status.ok()) return status;                    // если не создалась — ошибка
    return engine_.Start();                             // стартуем низкоуровневый движок
}

Status Database::Stop() {                               // остановка
    return engine_.Stop();                              // гасим движок
}

StorageNodeEngine& Database::engine() {                 // доступ к движку для query processor
    return engine_;
}

} // namespace db
