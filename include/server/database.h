#pragma once
#include "../runtime/storage_node_engine.h"

namespace db {

class Database {
public:
    explicit Database(Config config);

    Status Start();
    Status Stop();

    StorageNodeEngine& engine();

private:
    StorageNodeEngine engine_;
};

}
