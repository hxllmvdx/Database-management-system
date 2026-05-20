#pragma once
#include <cstdint>

#include "common/status.h"
#include "lock_manager.h"
#include "transaction.h"

namespace db {

class TransactionManager {
public:
    explicit TransactionManager(LockManager* lock_manager = nullptr)
        : lock_manager_(lock_manager) {}

    Transaction Begin(IsolationLevel isolation);
    Status Commit(Transaction* txn);
    Status Rollback(Transaction* txn);

private:
    Status FinishTransaction(Transaction* txn, TransactionState target_state);

    LockManager* lock_manager_ = nullptr;
    std::uint64_t next_txn_id_ = 1;
};

}
