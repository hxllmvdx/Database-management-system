#pragma once
#include <cstdint>

#include "isolation_level.h"

namespace db {

enum class TransactionState {
    kActive,
    kCommitted,
    kAborted,
};

struct Transaction {
    std::uint64_t id = 0;
    IsolationLevel isolation = IsolationLevel::kReadCommitted;
    TransactionState state = TransactionState::kActive;
};

}
