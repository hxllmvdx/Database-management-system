#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "common/status.h"
#include "storage/row_id.h"
#include "transaction.h"

namespace db {

class LockManager {
public:
    Status LockShared(const Transaction& txn, RowId rid);
    Status LockExclusive(const Transaction& txn, RowId rid);
    Status Unlock(const Transaction& txn, RowId rid);
    Status UnlockAll(const Transaction& txn);

private:
    struct LockEntry {
        std::unordered_set<std::uint64_t> shared_holders;
        std::optional<std::uint64_t> exclusive_holder;
    };

    std::mutex mutex_;
    std::unordered_map<std::uint64_t, LockEntry> lock_table_;
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> txn_locks_;
};

}
