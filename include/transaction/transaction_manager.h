#pragma once
#include "transaction.h"

namespace db {

class TransactionManager {
public:
    Transaction Begin(IsolationLevel isolation);
    void Commit(const Transaction& txn);
    void Rollback(const Transaction& txn);
};

}
