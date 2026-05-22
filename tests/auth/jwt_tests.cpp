#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "auth/jwt_auth.h"
#include "common/config.h"

db::Config MakeConfig() {
    db::Config cfg;
    cfg.jwt_secret      = "test-secret-key-for-unit-tests";
    cfg.jwt_ttl_seconds = 3600;
    return cfg;
}

TEST(JwtAuthTest, IssueAndValidateToken) {
    db::JwtAuth auth(MakeConfig());

    std::string token;
    ASSERT_TRUE(auth.IssueToken("alice", &token).ok());
    EXPECT_FALSE(token.empty());

    std::string user_id;
    ASSERT_TRUE(auth.ValidateToken(token, &user_id).ok());
    EXPECT_EQ(user_id, "alice");
}

TEST(JwtAuthTest, ValidateInvalidToken) {
    db::JwtAuth auth(MakeConfig());

    std::string user_id;
    EXPECT_FALSE(auth.ValidateToken("not.a.jwt", &user_id).ok());
    EXPECT_FALSE(auth.ValidateToken("",           &user_id).ok());
}

TEST(JwtAuthTest, ValidateWrongSignature) {
    db::Config cfg1 = MakeConfig();
    db::Config cfg2 = MakeConfig();
    cfg2.jwt_secret = "different-secret";

    db::JwtAuth auth1(cfg1);
    db::JwtAuth auth2(cfg2);

    std::string token;
    ASSERT_TRUE(auth1.IssueToken("bob", &token).ok());

    std::string user_id;
    EXPECT_FALSE(auth2.ValidateToken(token, &user_id).ok());
}

TEST(JwtAuthTest, DifferentUsersGetDifferentTokens) {
    db::JwtAuth auth(MakeConfig());

    std::string t1, t2;
    ASSERT_TRUE(auth.IssueToken("alice", &t1).ok());
    ASSERT_TRUE(auth.IssueToken("bob",   &t2).ok());
    EXPECT_NE(t1, t2);
}

TEST(JwtAuthTest, NullOutputRejected) {
    db::JwtAuth auth(MakeConfig());
    EXPECT_FALSE(auth.IssueToken("alice",  nullptr).ok());
    EXPECT_FALSE(auth.ValidateToken("tok", nullptr).ok());
}
