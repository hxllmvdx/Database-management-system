#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../common/config.h"
#include "../common/status.h"

namespace db {

enum class TaskStatus : int {
    kQueued    = 0,
    kRunning   = 1,
    kCompleted = 2,
    kFailed    = 3,
    kCancelled = 4,
};

std::string TaskStatusName(TaskStatus s);

struct TaskInfo {
    std::string request_id;
    std::string sql;
    std::string database;
    std::string user_id;
    TaskStatus  status    = TaskStatus::kQueued;
    int         progress  = 0;   
    std::string result;          
    std::string error;           
    int64_t     submitted_ms = 0;
    int64_t     started_ms   = 0;
    int64_t     finished_ms  = 0;
    int         retry_count  = 0;
};

using TaskExecutor = std::function<Status(TaskInfo& task)>;

class TaskQueue {
public:
    explicit TaskQueue(const Config& config, TaskExecutor executor);

    
    Status Start();

    
    Status Stop();

    
    
    Status Submit(const std::string& sql,
                  const std::string& database,
                  const std::string& user_id,
                  std::string*       out_request_id);

    
    Status GetStatus(const std::string& request_id, TaskInfo* out) const;

    
    Status Cancel(const std::string& request_id);

private:
    void     WorkerLoop();
    Status   AppendWal(const TaskInfo& info);
    Status   ReplayWal();

    Config       config_;
    TaskExecutor executor_;
    std::string  wal_path_;

    mutable std::mutex              mu_;
    std::condition_variable         cv_;
    std::deque<TaskInfo>            pending_queue_;
    std::unordered_map<std::string, TaskInfo> task_map_;

    std::vector<std::thread> workers_;
    std::atomic<bool>        running_{false};
    static constexpr std::size_t kMaxCompletedResults = 10000;
};

}  
