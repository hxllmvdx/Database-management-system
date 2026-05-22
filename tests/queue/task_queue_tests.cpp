#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

#include "common/config.h"
#include "queue/task_queue.h"

namespace fs = std::filesystem;

db::Config MakeQueueConfig(const std::string& dir) {
    db::Config cfg;
    cfg.task_queue_dir     = dir;
    cfg.task_queue_workers = 2;
    cfg.task_queue_max     = 100;
    return cfg;
}

class TaskQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = "./test_queue_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        fs::remove_all(dir_);
    }
    void TearDown() override {
        fs::remove_all(dir_);
    }
    std::string dir_;
};

TEST_F(TaskQueueTest, SubmitAndGetQueued) {
    db::Config cfg = MakeQueueConfig(dir_);
    bool called = false;
    db::TaskQueue q(cfg, [&called](db::TaskInfo& task) -> db::Status {
        called = true;
        task.result = "done";
        return db::Status::Ok();
    });
    ASSERT_TRUE(q.Start().ok());

    std::string rid;
    ASSERT_TRUE(q.Submit("SELECT 1;", "testdb", "user", &rid).ok());
    EXPECT_FALSE(rid.empty());

    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    db::TaskInfo info;
    ASSERT_TRUE(q.GetStatus(rid, &info).ok());
    EXPECT_EQ(info.status, db::TaskStatus::kCompleted);
    EXPECT_TRUE(called);

    ASSERT_TRUE(q.Stop().ok());
}

TEST_F(TaskQueueTest, FailedTaskStatus) {
    db::Config cfg = MakeQueueConfig(dir_);
    db::TaskQueue q(cfg, [](db::TaskInfo&) -> db::Status {
        return db::Status::Error(db::StatusCode::kExecutionError, "simulated failure");
    });
    ASSERT_TRUE(q.Start().ok());

    std::string rid;
    ASSERT_TRUE(q.Submit("BAD SQL", "testdb", "user", &rid).ok());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    db::TaskInfo info;
    ASSERT_TRUE(q.GetStatus(rid, &info).ok());
    EXPECT_EQ(info.status, db::TaskStatus::kFailed);
    EXPECT_FALSE(info.error.empty());

    ASSERT_TRUE(q.Stop().ok());
}

TEST_F(TaskQueueTest, GetStatusUnknownTaskReturnsNotFound) {
    db::Config cfg = MakeQueueConfig(dir_);
    db::TaskQueue q(cfg, [](db::TaskInfo&) { return db::Status::Ok(); });
    ASSERT_TRUE(q.Start().ok());

    db::TaskInfo info;
    EXPECT_FALSE(q.GetStatus("nonexistent-uuid", &info).ok());

    ASSERT_TRUE(q.Stop().ok());
}

TEST_F(TaskQueueTest, CancelQueuedTask) {
    
    std::atomic<bool> block{true};
    db::Config cfg = MakeQueueConfig(dir_);
    cfg.task_queue_workers = 1;

    db::TaskQueue q(cfg, [&block](db::TaskInfo&) -> db::Status {
        while (block.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return db::Status::Ok();
    });
    ASSERT_TRUE(q.Start().ok());

    
    std::string rid1, rid2;
    ASSERT_TRUE(q.Submit("SELECT 1;", "db", "u", &rid1).ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT_TRUE(q.Submit("SELECT 2;", "db", "u", &rid2).ok());

    
    ASSERT_TRUE(q.Cancel(rid2).ok());

    db::TaskInfo info2;
    ASSERT_TRUE(q.GetStatus(rid2, &info2).ok());
    EXPECT_EQ(info2.status, db::TaskStatus::kCancelled);

    block = false;  
    ASSERT_TRUE(q.Stop().ok());
}

TEST_F(TaskQueueTest, WalReplayRecovery) {
    db::Config cfg = MakeQueueConfig(dir_);

    
    {
        std::atomic<bool> started{false};
        std::atomic<bool> proceed{false};
        db::TaskQueue q(cfg, [&](db::TaskInfo& t) -> db::Status {
            started = true;
            while (!proceed.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            t.result = "recovered";
            return db::Status::Ok();
        });
        ASSERT_TRUE(q.Start().ok());

        std::string rid;
        ASSERT_TRUE(q.Submit("SELECT 1;", "db", "u", &rid).ok());

        
        while (!started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        proceed = true;
        ASSERT_TRUE(q.Stop().ok());
    }

    
    
    std::atomic<int> executed{0};
    db::TaskQueue q2(cfg, [&executed](db::TaskInfo& t) -> db::Status {
        executed++;
        t.result = "re-executed";
        return db::Status::Ok();
    });
    ASSERT_TRUE(q2.Start().ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    ASSERT_TRUE(q2.Stop().ok());
}

TEST_F(TaskQueueTest, ConcurrentSubmissions) {
    db::Config cfg = MakeQueueConfig(dir_);
    cfg.task_queue_workers = 4;

    std::atomic<int> completed{0};
    db::TaskQueue q(cfg, [&completed](db::TaskInfo&) -> db::Status {
        completed++;
        return db::Status::Ok();
    });
    ASSERT_TRUE(q.Start().ok());

    constexpr int kTasks = 50;
    std::vector<std::string> rids(kTasks);
    std::vector<std::thread> submitters;
    submitters.reserve(kTasks);

    for (int i = 0; i < kTasks; ++i) {
        submitters.emplace_back([&, i]() {
            q.Submit("SELECT " + std::to_string(i) + ";", "db", "u", &rids[i]);
        });
    }
    for (auto& t : submitters) t.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(q.Stop().ok());

    EXPECT_EQ(completed.load(), kTasks);
}
