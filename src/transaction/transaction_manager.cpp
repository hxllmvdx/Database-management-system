#include "transaction/transaction_manager.h"

namespace db {

Transaction TransactionManager::Begin(IsolationLevel isolation) {
    return Transaction{
        .id = next_txn_id_++,
        .isolation = isolation,
        .state = TransactionState::kActive,
    };
}

Status TransactionManager::Commit(Transaction* txn) {
    return FinishTransaction(txn, TransactionState::kCommitted);
}

Status TransactionManager::Rollback(Transaction* txn) {
    return FinishTransaction(txn, TransactionState::kAborted);
}

Status TransactionManager::FinishTransaction(Transaction* txn, TransactionState target_state) {
    if (txn == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "txn must not be nullptr");
    }
    if (txn->state != TransactionState::kActive) {
        return Status::Error(StatusCode::kTransactionError, "transaction is not active");
    }

    txn->state = target_state;
    if (lock_manager_ != nullptr) {
        Status unlock_status = lock_manager_->UnlockAll(*txn);
        if (!unlock_status.ok()) {
            return unlock_status;
        }
    }

    return Status::Ok();
}

}
