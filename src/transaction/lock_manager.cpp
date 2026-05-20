#include "transaction/lock_manager.h"

namespace db {

namespace {

bool CanAcquireLocks(const Transaction& txn) {
    return txn.state == TransactionState::kActive;
}

bool CanUnlock(const Transaction& txn) {
    return txn.state == TransactionState::kActive ||
           txn.state == TransactionState::kCommitted ||
           txn.state == TransactionState::kAborted;
}

void RememberTxnLockOwnership(std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>>& txn_locks,
                              std::uint64_t txn_id, std::uint64_t rid_value) {
    txn_locks[txn_id].insert(rid_value);
}

void ForgetTxnLockOwnership(std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>>& txn_locks,
                            std::uint64_t txn_id, std::uint64_t rid_value) {
    auto txn_it = txn_locks.find(txn_id);
    if (txn_it == txn_locks.end()) {
        return;
    }

    txn_it->second.erase(rid_value);
    if (txn_it->second.empty()) {
        txn_locks.erase(txn_it);
    }
}

}  // namespace

Status LockManager::LockShared(const Transaction& txn, RowId rid) {
    if (!CanAcquireLocks(txn)) {
        return Status::Error(StatusCode::kTransactionError, "transaction is not active");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    LockEntry& entry = lock_table_[rid.value];

    if (entry.exclusive_holder.has_value() && entry.exclusive_holder.value() != txn.id) {
        return Status::Error(StatusCode::kTransactionError, "shared lock conflict");
    }

    entry.shared_holders.insert(txn.id);
    RememberTxnLockOwnership(txn_locks_, txn.id, rid.value);
    return Status::Ok();
}

Status LockManager::LockExclusive(const Transaction& txn, RowId rid) {
    if (!CanAcquireLocks(txn)) {
        return Status::Error(StatusCode::kTransactionError, "transaction is not active");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    LockEntry& entry = lock_table_[rid.value];

    if (entry.exclusive_holder == txn.id) {
        RememberTxnLockOwnership(txn_locks_, txn.id, rid.value);
        return Status::Ok();
    }

    if (entry.exclusive_holder.has_value() && entry.exclusive_holder.value() != txn.id) {
        return Status::Error(StatusCode::kTransactionError, "exclusive lock conflict");
    }

    if (entry.shared_holders.empty()) {
        entry.exclusive_holder = txn.id;
        RememberTxnLockOwnership(txn_locks_, txn.id, rid.value);
        return Status::Ok();
    }

    const bool holds_only_shared_lock =
        entry.shared_holders.size() == 1U && entry.shared_holders.contains(txn.id);
    if (holds_only_shared_lock) {
        entry.shared_holders.erase(txn.id);
        entry.exclusive_holder = txn.id;
        RememberTxnLockOwnership(txn_locks_, txn.id, rid.value);
        return Status::Ok();
    }

    return Status::Error(StatusCode::kTransactionError, "cannot upgrade shared lock to exclusive");
}

Status LockManager::Unlock(const Transaction& txn, RowId rid) {
    if (!CanUnlock(txn)) {
        return Status::Error(StatusCode::kTransactionError, "transaction state cannot unlock");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    auto entry_it = lock_table_.find(rid.value);
    if (entry_it == lock_table_.end()) {
        ForgetTxnLockOwnership(txn_locks_, txn.id, rid.value);
        return Status::Ok();
    }

    LockEntry& entry = entry_it->second;
    if (entry.exclusive_holder == txn.id) {
        entry.exclusive_holder.reset();
    }
    entry.shared_holders.erase(txn.id);

    if (!entry.exclusive_holder.has_value() && entry.shared_holders.empty()) {
        lock_table_.erase(entry_it);
    }
    ForgetTxnLockOwnership(txn_locks_, txn.id, rid.value);

    return Status::Ok();
}

Status LockManager::UnlockAll(const Transaction& txn) {
    if (!CanUnlock(txn)) {
        return Status::Error(StatusCode::kTransactionError, "transaction state cannot unlock");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    auto txn_it = txn_locks_.find(txn.id);
    if (txn_it == txn_locks_.end()) {
        return Status::Ok();
    }

    std::unordered_set<std::uint64_t> held_rids = txn_it->second;
    for (std::uint64_t rid_value : held_rids) {
        auto entry_it = lock_table_.find(rid_value);
        if (entry_it == lock_table_.end()) {
            continue;
        }

        LockEntry& entry = entry_it->second;
        if (entry.exclusive_holder == txn.id) {
            entry.exclusive_holder.reset();
        }
        entry.shared_holders.erase(txn.id);
        if (!entry.exclusive_holder.has_value() && entry.shared_holders.empty()) {
            lock_table_.erase(entry_it);
        }
    }

    txn_locks_.erase(txn_it);
    return Status::Ok();
}

}
