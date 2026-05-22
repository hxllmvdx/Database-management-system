#include <filesystem>
#include <gtest/gtest.h>

#include "auth/rbac.h"

namespace fs = std::filesystem;

class RbacTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_path_ = "./test_rbac_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
        store_ = std::make_unique<db::RbacStore>(store_path_);
        ASSERT_TRUE(store_->Open().ok());
    }
    void TearDown() override {
        store_.reset();
        fs::remove(store_path_);
        fs::remove(store_path_ + ".tmp");
    }

    std::string                    store_path_;
    std::unique_ptr<db::RbacStore> store_;
};

TEST_F(RbacTest, DefaultAdminExists) {
    EXPECT_TRUE(store_->UserExists("admin"));
}

TEST_F(RbacTest, AdminHasAllPermissions) {
    EXPECT_TRUE(store_->HasPermission("admin", db::Permission::kRead));
    EXPECT_TRUE(store_->HasPermission("admin", db::Permission::kWrite));
    EXPECT_TRUE(store_->HasPermission("admin", db::Permission::kCreateTable));
    EXPECT_TRUE(store_->HasPermission("admin", db::Permission::kDropDatabase));
}

TEST_F(RbacTest, CreateAndVerifyUser) {
    ASSERT_TRUE(store_->CreateUser("alice", "secret123").ok());
    EXPECT_TRUE(store_->UserExists("alice"));

    bool ok = false;
    ASSERT_TRUE(store_->CheckPassword("alice", "secret123", &ok).ok());
    EXPECT_TRUE(ok);

    ASSERT_TRUE(store_->CheckPassword("alice", "wrongpassword", &ok).ok());
    EXPECT_FALSE(ok);
}

TEST_F(RbacTest, CreateDuplicateUserFails) {
    ASSERT_TRUE(store_->CreateUser("bob", "pass").ok());
    EXPECT_FALSE(store_->CreateUser("bob", "other").ok());
}

TEST_F(RbacTest, DropUserRemovesUser) {
    ASSERT_TRUE(store_->CreateUser("charlie", "pass").ok());
    ASSERT_TRUE(store_->DropUser("charlie").ok());
    EXPECT_FALSE(store_->UserExists("charlie"));
}

TEST_F(RbacTest, GrantDirectPermission) {
    ASSERT_TRUE(store_->CreateUser("reader", "pass").ok());
    EXPECT_FALSE(store_->HasPermission("reader", db::Permission::kRead));

    ASSERT_TRUE(store_->GrantToUser("reader", db::Permission::kRead).ok());
    EXPECT_TRUE(store_->HasPermission("reader", db::Permission::kRead));
    EXPECT_FALSE(store_->HasPermission("reader", db::Permission::kWrite));
}

TEST_F(RbacTest, RevokePermission) {
    ASSERT_TRUE(store_->CreateUser("rw", "pass").ok());
    ASSERT_TRUE(store_->GrantToUser("rw", db::Permission::kRead).ok());
    ASSERT_TRUE(store_->GrantToUser("rw", db::Permission::kWrite).ok());

    ASSERT_TRUE(store_->RevokeFromUser("rw", db::Permission::kWrite).ok());
    EXPECT_TRUE(store_->HasPermission("rw",  db::Permission::kRead));
    EXPECT_FALSE(store_->HasPermission("rw", db::Permission::kWrite));
}

TEST_F(RbacTest, GroupPermissionInheritance) {
    ASSERT_TRUE(store_->CreateUser("groupmember", "pass").ok());
    ASSERT_TRUE(store_->CreateGroup("editors").ok());
    ASSERT_TRUE(store_->GrantToGroup("editors", db::Permission::kWrite).ok());
    ASSERT_TRUE(store_->AddUserToGroup("groupmember", "editors").ok());

    EXPECT_TRUE(store_->HasPermission("groupmember", db::Permission::kWrite));
    EXPECT_FALSE(store_->HasPermission("groupmember", db::Permission::kRead));
}

TEST_F(RbacTest, ParsePermissionNames) {
    db::Permission perm{};
    ASSERT_TRUE(db::ParsePermission("READ",         &perm).ok());
    EXPECT_EQ(perm, db::Permission::kRead);
    ASSERT_TRUE(db::ParsePermission("WRITE",        &perm).ok());
    EXPECT_EQ(perm, db::Permission::kWrite);
    ASSERT_TRUE(db::ParsePermission("CREATE_TABLE", &perm).ok());
    EXPECT_EQ(perm, db::Permission::kCreateTable);
    EXPECT_FALSE(db::ParsePermission("UNKNOWN",     &perm).ok());
}

TEST_F(RbacTest, NonExistentUserHasNoPermissions) {
    EXPECT_FALSE(store_->HasPermission("nobody", db::Permission::kRead));
}

TEST_F(RbacTest, DropGroupRevokesFromMembers) {
    ASSERT_TRUE(store_->CreateUser("member", "pass").ok());
    ASSERT_TRUE(store_->CreateGroup("tmpgroup").ok());
    ASSERT_TRUE(store_->GrantToGroup("tmpgroup", db::Permission::kWrite).ok());
    ASSERT_TRUE(store_->AddUserToGroup("member", "tmpgroup").ok());

    EXPECT_TRUE(store_->HasPermission("member", db::Permission::kWrite));
    ASSERT_TRUE(store_->DropGroup("tmpgroup").ok());
    EXPECT_FALSE(store_->HasPermission("member", db::Permission::kWrite));
}
