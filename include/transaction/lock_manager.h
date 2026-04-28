#pragma once
#include "../storage/row_id.h"
#include "transaction.h"

namespace db {

class LockManager {
public:
    bool LockShared(const Transaction& txn, RowId rid);
    bool LockExclusive(const Transaction& txn, RowId rid);
    void Unlock(const Transaction& txn, RowId rid);
};

}
