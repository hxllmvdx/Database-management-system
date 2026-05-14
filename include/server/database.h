#pragma once
#include "../runtime/storage_node_engine.h"

namespace db {

class Database {
public:
    explicit Database(Config config);

    Status Start();
    Status Stop();

    StorageNodeEngine& engine();
    const Config& config() const; // доступ к конфигурации

private:
    Config config_; // сохраняем конфиг
    StorageNodeEngine engine_; // движок хранения
};

}
