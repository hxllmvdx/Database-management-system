#include <gtest/gtest.h>

#include "transaction/transaction_manager.h"

namespace {

TEST(TransactionManagerTest, BeginAssignsMonotonicIds) {
    db::TransactionManager manager;

    const db::Transaction first = manager.Begin(db::IsolationLevel::kReadCommitted);
    const db::Transaction second = manager.Begin(db::IsolationLevel::kSerializable);

    EXPECT_LT(first.id, second.id);
    EXPECT_EQ(first.id + 1U, second.id);
}

TEST(TransactionManagerTest, BeginSetsIsolationAndActiveState) {
    db::TransactionManager manager;

    const db::Transaction txn = manager.Begin(db::IsolationLevel::kRepeatableRead);

    EXPECT_EQ(txn.isolation, db::IsolationLevel::kRepeatableRead);
    EXPECT_EQ(txn.state, db::TransactionState::kActive);
}

TEST(TransactionManagerTest, CommitMovesActiveTransactionToCommitted) {
    db::TransactionManager manager;
    db::Transaction txn = manager.Begin(db::IsolationLevel::kReadCommitted);

    const db::Status status = manager.Commit(&txn);

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_EQ(txn.state, db::TransactionState::kCommitted);
}

TEST(TransactionManagerTest, RollbackMovesActiveTransactionToAborted) {
    db::TransactionManager manager;
    db::Transaction txn = manager.Begin(db::IsolationLevel::kReadCommitted);

    const db::Status status = manager.Rollback(&txn);

    ASSERT_TRUE(status.ok()) << status.message();
    EXPECT_EQ(txn.state, db::TransactionState::kAborted);
}

TEST(TransactionManagerTest, CommitRejectsNullptr) {
    db::TransactionManager manager;

    const db::Status status = manager.Commit(nullptr);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

TEST(TransactionManagerTest, RollbackRejectsNullptr) {
    db::TransactionManager manager;

    const db::Status status = manager.Rollback(nullptr);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), db::StatusCode::kInvalidArgument);
}

TEST(TransactionManagerTest, CommitRejectsNonActiveTransaction) {
    db::TransactionManager manager;
    db::Transaction committed = manager.Begin(db::IsolationLevel::kReadCommitted);
    db::Transaction aborted = manager.Begin(db::IsolationLevel::kReadCommitted);

    ASSERT_TRUE(manager.Commit(&committed).ok());
    ASSERT_TRUE(manager.Rollback(&aborted).ok());

    const db::Status committed_status = manager.Commit(&committed);
    const db::Status aborted_status = manager.Commit(&aborted);

    EXPECT_FALSE(committed_status.ok());
    EXPECT_EQ(committed_status.code(), db::StatusCode::kTransactionError);
    EXPECT_FALSE(aborted_status.ok());
    EXPECT_EQ(aborted_status.code(), db::StatusCode::kTransactionError);
}

TEST(TransactionManagerTest, CommitReleasesTransactionLocksWhenLockManagerIsAttached) {
    db::LockManager lock_manager;
    db::TransactionManager manager(&lock_manager);
    db::Transaction txn = manager.Begin(db::IsolationLevel::kReadCommitted);

    ASSERT_TRUE(lock_manager.LockExclusive(txn, db::RowId{21}).ok());

    const db::Status commit_status = manager.Commit(&txn);
    const db::Status next_lock_status = lock_manager.LockExclusive(
        db::Transaction{.id = 2, .isolation = db::IsolationLevel::kReadCommitted, .state = db::TransactionState::kActive},
        db::RowId{21});

    ASSERT_TRUE(commit_status.ok()) << commit_status.message();
    EXPECT_EQ(txn.state, db::TransactionState::kCommitted);
    EXPECT_TRUE(next_lock_status.ok()) << next_lock_status.message();
}

TEST(TransactionManagerTest, RollbackReleasesTransactionLocksWhenLockManagerIsAttached) {
    db::LockManager lock_manager;
    db::TransactionManager manager(&lock_manager);
    db::Transaction txn = manager.Begin(db::IsolationLevel::kReadCommitted);

    ASSERT_TRUE(lock_manager.LockShared(txn, db::RowId{22}).ok());

    const db::Status rollback_status = manager.Rollback(&txn);
    const db::Status next_lock_status = lock_manager.LockExclusive(
        db::Transaction{.id = 3, .isolation = db::IsolationLevel::kReadCommitted, .state = db::TransactionState::kActive},
        db::RowId{22});

    ASSERT_TRUE(rollback_status.ok()) << rollback_status.message();
    EXPECT_EQ(txn.state, db::TransactionState::kAborted);
    EXPECT_TRUE(next_lock_status.ok()) << next_lock_status.message();
}

}
