#pragma once
#include <string>
#include "../runtime/storage_node_engine.h"

namespace db {

struct ExecutorContext {
    std::string current_db;
    StorageNodeEngine* engine = nullptr;
};

}
