#include <gtest/gtest.h>

#include "transaction/lock_manager.h"

namespace {

db::Transaction MakeTransaction(std::uint64_t id, db::TransactionState state = db::TransactionState::kActive) {
    return db::Transaction{
        .id = id,
        .isolation = db::IsolationLevel::kReadCommitted,
        .state = state,
    };
}

TEST(LockManagerTest, SharedLocksAreCompatible) {
    db::LockManager manager;
    const db::RowId rid{11};

    const db::Status first = manager.LockShared(MakeTransaction(1), rid);
    const db::Status second = manager.LockShared(MakeTransaction(2), rid);

    EXPECT_TRUE(first.ok()) << first.message();
    EXPECT_TRUE(second.ok()) << second.message();
}

TEST(LockManagerTest, ExclusiveConflictsWithSharedFromAnotherTransaction) {
    db::LockManager manager;
    const db::RowId rid{12};

    ASSERT_TRUE(manager.LockShared(MakeTransaction(1), rid).ok());

    const db::Status status = manager.LockExclusive(MakeTransaction(2), rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kTransactionError);
}

TEST(LockManagerTest, ExclusiveConflictsWithExclusiveFromAnotherTransaction) {
    db::LockManager manager;
    const db::RowId rid{13};

    ASSERT_TRUE(manager.LockExclusive(MakeTransaction(1), rid).ok());

    const db::Status status = manager.LockExclusive(MakeTransaction(2), rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kTransactionError);
}

TEST(LockManagerTest, ExclusiveLockCanBeReenteredBySameTransaction) {
    db::LockManager manager;
    const db::RowId rid{14};
    const db::Transaction txn = MakeTransaction(1);

    const db::Status first = manager.LockExclusive(txn, rid);
    const db::Status second = manager.LockExclusive(txn, rid);

    EXPECT_TRUE(first.ok()) << first.message();
    EXPECT_TRUE(second.ok()) << second.message();
}

TEST(LockManagerTest, SharedToExclusiveUpgradeWorksWhenTransactionIsOnlyHolder) {
    db::LockManager manager;
    const db::RowId rid{15};
    const db::Transaction txn = MakeTransaction(1);

    ASSERT_TRUE(manager.LockShared(txn, rid).ok());

    const db::Status status = manager.LockExclusive(txn, rid);

    EXPECT_TRUE(status.ok()) << status.message();
}

TEST(LockManagerTest, SharedToExclusiveUpgradeFailsWhenOtherSharedHoldersExist) {
    db::LockManager manager;
    const db::RowId rid{16};

    ASSERT_TRUE(manager.LockShared(MakeTransaction(1), rid).ok());
    ASSERT_TRUE(manager.LockShared(MakeTransaction(2), rid).ok());

    const db::Status status = manager.LockExclusive(MakeTransaction(1), rid);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kTransactionError);
}

TEST(LockManagerTest, UnlockRemovesHeldLocks) {
    db::LockManager manager;
    const db::RowId rid{17};
    const db::Transaction first = MakeTransaction(1);
    const db::Transaction second = MakeTransaction(2);

    ASSERT_TRUE(manager.LockShared(first, rid).ok());
    ASSERT_TRUE(manager.Unlock(first, rid).ok());

    const db::Status status = manager.LockExclusive(second, rid);

    EXPECT_TRUE(status.ok()) << status.message();
}

TEST(LockManagerTest, InactiveTransactionCannotAcquireLocks) {
    db::LockManager manager;
    const db::RowId rid{18};

    const db::Status committed_status =
        manager.LockShared(MakeTransaction(1, db::TransactionState::kCommitted), rid);
    const db::Status aborted_status =
        manager.LockExclusive(MakeTransaction(2, db::TransactionState::kAborted), rid);

    EXPECT_FALSE(committed_status.ok());
    EXPECT_EQ(committed_status.code(), db::StatusCode::kTransactionError);
    EXPECT_FALSE(aborted_status.ok());
    EXPECT_EQ(aborted_status.code(), db::StatusCode::kTransactionError);
}

TEST(LockManagerTest, UnlockOnMissingLockIsSafe) {
    db::LockManager manager;

    const db::Status status = manager.Unlock(MakeTransaction(1), db::RowId{19});

    EXPECT_TRUE(status.ok()) << status.message();
}

TEST(LockManagerTest, UnlockAllReleasesEveryHeldLockForTransaction) {
    db::LockManager manager;
    const db::Transaction txn = MakeTransaction(1);

    ASSERT_TRUE(manager.LockShared(txn, db::RowId{31}).ok());
    ASSERT_TRUE(manager.LockExclusive(txn, db::RowId{32}).ok());

    const db::Status unlock_status = manager.UnlockAll(txn);
    const db::Status shared_after = manager.LockExclusive(MakeTransaction(2), db::RowId{31});
    const db::Status exclusive_after = manager.LockExclusive(MakeTransaction(3), db::RowId{32});

    ASSERT_TRUE(unlock_status.ok()) << unlock_status.message();
    EXPECT_TRUE(shared_after.ok()) << shared_after.message();
    EXPECT_TRUE(exclusive_after.ok()) << exclusive_after.message();
}

}
