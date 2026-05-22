#include "queue/task_queue.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace db {

std::string TaskStatusName(TaskStatus s) {
    switch (s) {
        case TaskStatus::kQueued:    return "queued";
        case TaskStatus::kRunning:   return "running";
        case TaskStatus::kCompleted: return "completed";
        case TaskStatus::kFailed:    return "failed";
        case TaskStatus::kCancelled: return "cancelled";
    }
    return "unknown";
}

static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string GenerateUuidV4() {
    
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto r = [&]() { return dist(rng); };
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << r() << '-';
    ss << std::setw(4) << (r() & 0xFFFF) << '-';
    ss << std::setw(4) << ((r() & 0x0FFF) | 0x4000) << '-';
    ss << std::setw(4) << ((r() & 0x3FFF) | 0x8000) << '-';
    ss << std::setw(8) << r() << std::setw(4) << (r() & 0xFFFF);
    return ss.str();
}

static bool IsTerminal(TaskStatus s) {
    return s == TaskStatus::kCompleted ||
           s == TaskStatus::kFailed    ||
           s == TaskStatus::kCancelled;
}

static json TaskToJson(const TaskInfo& t) {
    json j;
    j["request_id"]   = t.request_id;
    j["sql"]          = t.sql;
    j["database"]     = t.database;
    j["user_id"]      = t.user_id;
    j["status"]       = static_cast<int>(t.status);
    j["progress"]     = t.progress;
    j["result"]       = t.result;
    j["error"]        = t.error;
    j["submitted_ms"] = t.submitted_ms;
    j["started_ms"]   = t.started_ms;
    j["finished_ms"]  = t.finished_ms;
    j["retry_count"]  = t.retry_count;
    return j;
}

static TaskInfo TaskFromJson(const json& j) {
    TaskInfo t;
    t.request_id   = j.at("request_id").get<std::string>();
    t.sql          = j.at("sql").get<std::string>();
    t.database     = j.at("database").get<std::string>();
    t.user_id      = j.at("user_id").get<std::string>();
    t.status       = static_cast<TaskStatus>(j.at("status").get<int>());
    t.progress     = j.at("progress").get<int>();
    t.result       = j.at("result").get<std::string>();
    t.error        = j.at("error").get<std::string>();
    t.submitted_ms = j.at("submitted_ms").get<int64_t>();
    t.started_ms   = j.at("started_ms").get<int64_t>();
    t.finished_ms  = j.at("finished_ms").get<int64_t>();
    t.retry_count  = j.at("retry_count").get<int>();
    return t;
}

TaskQueue::TaskQueue(const Config& config, TaskExecutor executor)
    : config_(config), executor_(std::move(executor)) {
    fs::create_directories(config_.task_queue_dir);
    wal_path_ = (fs::path(config_.task_queue_dir) / "tasks.wal").string();
}

Status TaskQueue::Start() {
    Status s = ReplayWal();
    if (!s.ok()) return s;

    running_ = true;
    const std::size_t n = std::max<std::size_t>(1, config_.task_queue_workers);
    workers_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        workers_.emplace_back(&TaskQueue::WorkerLoop, this);
    }
    return Status::Ok();
}

Status TaskQueue::Stop() {
    running_ = false;
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    return Status::Ok();
}

Status TaskQueue::Submit(const std::string& sql,
                         const std::string& database,
                         const std::string& user_id,
                         std::string* out_request_id) {
    if (out_request_id == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_request_id is null");
    }

    TaskInfo task;
    task.request_id   = GenerateUuidV4();
    task.sql          = sql;
    task.database     = database;
    task.user_id      = user_id;
    task.status       = TaskStatus::kQueued;
    task.submitted_ms = NowMs();

    {
        std::unique_lock<std::mutex> lock(mu_);
        if (pending_queue_.size() >= config_.task_queue_max) {
            return Status::Error(StatusCode::kInternalError, "task queue is full");
        }
        
        Status ws = AppendWal(task);
        if (!ws.ok()) return ws;

        task_map_[task.request_id] = task;
        pending_queue_.push_back(task);
    }
    cv_.notify_one();

    *out_request_id = task.request_id;
    return Status::Ok();
}

Status TaskQueue::GetStatus(const std::string& request_id, TaskInfo* out) const {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = task_map_.find(request_id);
    if (it == task_map_.end()) {
        return Status::Error(StatusCode::kNotFound,
                             "task not found: " + request_id);
    }
    *out = it->second;
    return Status::Ok();
}

Status TaskQueue::Cancel(const std::string& request_id) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = task_map_.find(request_id);
    if (it == task_map_.end()) {
        return Status::Error(StatusCode::kNotFound,
                             "task not found: " + request_id);
    }
    if (IsTerminal(it->second.status)) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "task is already in terminal state");
    }
    it->second.status = TaskStatus::kCancelled;
    it->second.finished_ms = NowMs();

    
    for (auto qit = pending_queue_.begin(); qit != pending_queue_.end(); ++qit) {
        if (qit->request_id == request_id) {
            pending_queue_.erase(qit);
            break;
        }
    }
    return AppendWal(it->second);
}

void TaskQueue::WorkerLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        TaskInfo task;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return !pending_queue_.empty() || !running_.load();
            });
            if (!running_.load() && pending_queue_.empty()) break;
            if (pending_queue_.empty()) continue;

            task = pending_queue_.front();
            pending_queue_.pop_front();

            
            auto it = task_map_.find(task.request_id);
            if (it != task_map_.end() &&
                it->second.status == TaskStatus::kCancelled) {
                continue;
            }

            
            if (it != task_map_.end()) {
                it->second.status    = TaskStatus::kRunning;
                it->second.started_ms = NowMs();
                task = it->second;
            }
            AppendWal(task);
        }

        
        const Status result = executor_(task);

        {
            std::unique_lock<std::mutex> lock(mu_);
            auto it = task_map_.find(task.request_id);
            if (it == task_map_.end()) continue;

            if (it->second.status == TaskStatus::kCancelled) continue;

            if (result.ok()) {
                it->second.status      = TaskStatus::kCompleted;
                it->second.progress    = 100;
                it->second.result      = task.result;
            } else {
                it->second.status  = TaskStatus::kFailed;
                it->second.error   = result.message();
            }
            it->second.finished_ms = NowMs();
            AppendWal(it->second);

            
            if (task_map_.size() > kMaxCompletedResults) {
                
                
                auto oldest = task_map_.begin();
                if (IsTerminal(oldest->second.status) &&
                    oldest->first != task.request_id) {
                    task_map_.erase(oldest);
                }
            }
        }
    }
}

Status TaskQueue::AppendWal(const TaskInfo& info) {
    
    std::ofstream f(wal_path_, std::ios::app);
    if (!f.is_open()) {
        return Status::Error(StatusCode::kIoError,
                             "cannot open task WAL: " + wal_path_);
    }
    f << TaskToJson(info).dump() << '\n';
    return Status::Ok();
}

Status TaskQueue::ReplayWal() {
    if (!fs::exists(wal_path_)) return Status::Ok();

    std::ifstream f(wal_path_);
    if (!f.is_open()) {
        return Status::Error(StatusCode::kIoError,
                             "cannot open task WAL for replay: " + wal_path_);
    }

    
    std::unordered_map<std::string, TaskInfo> latest;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try {
            const TaskInfo task = TaskFromJson(json::parse(line));
            latest[task.request_id] = task;
        } catch (...) {
            
        }
    }

    
    for (auto& [id, task] : latest) {
        task_map_[id] = task;
        if (!IsTerminal(task.status)) {
            task.status = TaskStatus::kQueued;
            task_map_[id].status = TaskStatus::kQueued;
            pending_queue_.push_back(task);
        }
    }

    
    {
        const std::string tmp = wal_path_ + ".tmp";
        std::ofstream out(tmp);
        if (!out.is_open()) {
            return Status::Error(StatusCode::kIoError,
                                 "cannot compact task WAL: " + tmp);
        }
        for (const auto& [id, task] : task_map_) {
            out << TaskToJson(task).dump() << '\n';
        }
        std::error_code ec;
        fs::rename(tmp, wal_path_, ec);
        if (ec) {
            return Status::Error(StatusCode::kIoError,
                                 "cannot rename WAL after compact: " + ec.message());
        }
    }

    return Status::Ok();
}

}
