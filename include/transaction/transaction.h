#pragma once
#include <cstdint>
#include "isolation_level.h"

namespace db {

struct Transaction {
    std::uint64_t id = 0;
    IsolationLevel isolation = IsolationLevel::kReadCommitted;
};

}
