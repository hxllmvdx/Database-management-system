#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

#include "common/config.h"
#include "logging/logger.h"

namespace fs = std::filesystem;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_dir_ = "./test_logs_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        fs::remove_all(log_dir_);

        db::Config cfg;
        cfg.log_dir       = log_dir_;
        cfg.log_level     = "trace";
        cfg.log_max_mb    = 1;
        cfg.log_max_files = 3;
        db::Logger::Init(cfg);
    }

    void TearDown() override {
        db::Logger::Shutdown();
        fs::remove_all(log_dir_);
    }

    std::string LogContent() {
        const std::string path = log_dir_ + "/server.log";
        std::ifstream f(path);
        std::string s((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        return s;
    }

    std::string AccessLogContent() {
        const std::string path = log_dir_ + "/access.log";
        std::ifstream f(path);
        std::string s((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        return s;
    }

    std::string log_dir_;
};

TEST_F(LoggerTest, LogDirCreated) {
    EXPECT_TRUE(fs::exists(log_dir_));
}

TEST_F(LoggerTest, InfoMessageAppearsInLog) {
    db::Logger::Info("hello world", {{"key", "value"}});
    db::Logger::Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string content = LogContent();
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("hello world"), std::string::npos);
    EXPECT_NE(content.find("\"level\":\"info\""), std::string::npos);
    EXPECT_NE(content.find("\"key\":\"value\""), std::string::npos);
}

TEST_F(LoggerTest, AllLevelsFunnel) {
    db::Logger::Trace("trace-msg");
    db::Logger::Debug("debug-msg");
    db::Logger::Info("info-msg");
    db::Logger::Warn("warn-msg");
    db::Logger::Error("error-msg");
    db::Logger::Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string content = LogContent();
    EXPECT_NE(content.find("trace-msg"), std::string::npos);
    EXPECT_NE(content.find("debug-msg"), std::string::npos);
    EXPECT_NE(content.find("info-msg"),  std::string::npos);
    EXPECT_NE(content.find("warn-msg"),  std::string::npos);
    EXPECT_NE(content.find("error-msg"), std::string::npos);
}

TEST_F(LoggerTest, AccessLogEntryIsStructured) {
    db::AccessLogEntry entry;
    entry.timestamp_start = "2026-01-01T00:00:00.000Z";
    entry.timestamp_end   = "2026-01-01T00:00:00.015Z";
    entry.request_id      = "req-123";
    entry.client_id       = "cli-456";
    entry.sql             = "SELECT * FROM users;";
    entry.target_node     = "node-1";
    entry.shard_id        = "mydb";
    entry.duration_ms     = 15;
    entry.ok              = true;

    db::Logger::AccessLog(entry);
    db::Logger::Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::string content = AccessLogContent();
    EXPECT_NE(content.find("req-123"), std::string::npos);
    EXPECT_NE(content.find("cli-456"), std::string::npos);
    EXPECT_NE(content.find("SELECT * FROM users"), std::string::npos);
    EXPECT_NE(content.find("node-1"), std::string::npos);
    EXPECT_NE(content.find("\"status\":\"ok\""), std::string::npos);
}

TEST_F(LoggerTest, ThreadSafeMultipleWriters) {
    constexpr int kThreads  = 8;
    constexpr int kMsgsEach = 50;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kMsgsEach; ++i) {
                db::Logger::Info("thread-" + std::to_string(t) +
                                 "-msg-" + std::to_string(i));
            }
        });
    }
    for (auto& th : threads) th.join();

    db::Logger::Flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const std::string content = LogContent();
    
    EXPECT_FALSE(content.empty());
}

TEST_F(LoggerTest, NowIso8601Format) {
    const std::string ts = db::Logger::NowIso8601();
    
    EXPECT_EQ(ts.size(), 24u);
    EXPECT_EQ(ts[10], 'T');
    EXPECT_EQ(ts[23], 'Z');
}

TEST_F(LoggerTest, NowMsReasonable) {
    const int64_t ms = db::Logger::NowMs();
    
    EXPECT_GT(ms, int64_t{1704067200000});
}
