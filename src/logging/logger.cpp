#include "logging/logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace db {

std::string Logger::NowIso8601() {
    using namespace std::chrono;
    auto now   = system_clock::now();
    auto ms    = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = system_clock::to_time_t(now);
    std::tm tm_info{};
#ifdef _WIN32
    gmtime_s(&tm_info, &timer);
#else
    gmtime_r(&timer, &tm_info);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

int64_t Logger::NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const Config& config) {
    Instance().InitImpl(config);
}

void Logger::Shutdown() {
    Instance().ShutdownImpl();
}

void Logger::Flush() {
    Instance().FlushImpl();
}

void Logger::InitImpl(const Config& config) {
    ShutdownImpl();  

    log_dir_          = config.log_dir;
    max_bytes_        = config.log_max_mb * 1024 * 1024;
    max_files_        = config.log_max_files;

    const std::string& lv = config.log_level;
    if      (lv == "trace") min_level_ = LogLevel::kTrace;
    else if (lv == "debug") min_level_ = LogLevel::kDebug;
    else if (lv == "warn")  min_level_ = LogLevel::kWarn;
    else if (lv == "error") min_level_ = LogLevel::kError;
    else                    min_level_ = LogLevel::kInfo;

    fs::create_directories(log_dir_);
    server_log_path_ = (fs::path(log_dir_) / "server.log").string();
    access_log_path_ = (fs::path(log_dir_) / "access.log").string();

    initialized_ = true;
    running_     = true;
    writer_thread_ = std::thread(&Logger::WriterLoop, this);
}

void Logger::ShutdownImpl() {
    if (!initialized_) return;
    running_ = false;
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) writer_thread_.join();
    initialized_ = false;
}

void Logger::FlushImpl() {
    if (!initialized_) return;
    
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    queue_.push(LogEntry{});
    queue_cv_.notify_one();
}

void Logger::Push(LogEntry entry) {
    if (!initialized_) return;
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (queue_.size() >= max_queue_) {
        
        queue_.pop();
    }
    queue_.push(std::move(entry));
    queue_cv_.notify_one();
}

void Logger::WriterLoop() {
    while (true) {
        std::queue<LogEntry> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load();
            });
            std::swap(batch, queue_);
        }

        while (!batch.empty()) {
            LogEntry entry = std::move(batch.front());
            batch.pop();

            if (entry.json_line.empty()) continue;  

            if (entry.is_access) {
                WriteToFile(access_log_path_, entry.json_line);
            } else {
                WriteToFile(server_log_path_, entry.json_line);
            }
        }

        if (!running_.load()) {
            
            std::unique_lock<std::mutex> lock(queue_mutex_);
            while (!queue_.empty()) {
                LogEntry entry = std::move(queue_.front());
                queue_.pop();
                if (!entry.json_line.empty()) {
                    if (entry.is_access)
                        WriteToFile(access_log_path_, entry.json_line);
                    else
                        WriteToFile(server_log_path_, entry.json_line);
                }
            }
            break;
        }
    }
}

void Logger::WriteToFile(const std::string& path, const std::string& line) {
    MaybeRotate(path);
    std::ofstream f(path, std::ios::app);
    if (f.is_open()) {
        f << line << '\n';
    }
}

void Logger::MaybeRotate(const std::string& path) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size < max_bytes_) return;

    
    for (std::size_t i = max_files_ - 1; i >= 1; --i) {
        const fs::path old_name = path + "." + std::to_string(i);
        const fs::path new_name = path + "." + std::to_string(i + 1);
        if (fs::exists(old_name)) {
            if (i + 1 >= max_files_) {
                fs::remove(old_name, ec);
            } else {
                fs::rename(old_name, new_name, ec);
            }
        }
    }
    fs::rename(path, path + ".1", ec);
}

bool Logger::ShouldLog(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(min_level_);
}

static std::string LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace: return "trace";
        case LogLevel::kDebug: return "debug";
        case LogLevel::kInfo:  return "info";
        case LogLevel::kWarn:  return "warn";
        case LogLevel::kError: return "error";
    }
    return "info";
}

std::string Logger::BuildJson(LogLevel level,
                              const std::string& msg,
                              const std::map<std::string, std::string>& fields) const {
    json j;
    j["timestamp"] = NowIso8601();
    j["level"]     = LevelName(level);
    j["message"]   = msg;
    for (const auto& [k, v] : fields) {
        j[k] = v;
    }
    return j.dump();
}

void Logger::Trace(const std::string& msg,
                   const std::map<std::string, std::string>& fields) {
    auto& inst = Instance();
    if (!inst.ShouldLog(LogLevel::kTrace)) return;
    inst.Push({false, inst.BuildJson(LogLevel::kTrace, msg, fields)});
}

void Logger::Debug(const std::string& msg,
                   const std::map<std::string, std::string>& fields) {
    auto& inst = Instance();
    if (!inst.ShouldLog(LogLevel::kDebug)) return;
    inst.Push({false, inst.BuildJson(LogLevel::kDebug, msg, fields)});
}

void Logger::Info(const std::string& msg,
                  const std::map<std::string, std::string>& fields) {
    auto& inst = Instance();
    if (!inst.ShouldLog(LogLevel::kInfo)) return;
    inst.Push({false, inst.BuildJson(LogLevel::kInfo, msg, fields)});
}

void Logger::Warn(const std::string& msg,
                  const std::map<std::string, std::string>& fields) {
    auto& inst = Instance();
    if (!inst.ShouldLog(LogLevel::kWarn)) return;
    inst.Push({false, inst.BuildJson(LogLevel::kWarn, msg, fields)});
}

void Logger::Error(const std::string& msg,
                   const std::map<std::string, std::string>& fields) {
    auto& inst = Instance();
    if (!inst.ShouldLog(LogLevel::kError)) return;
    inst.Push({false, inst.BuildJson(LogLevel::kError, msg, fields)});
}

void Logger::AccessLog(const AccessLogEntry& entry) {
    auto& inst = Instance();
    if (!inst.initialized_) return;

    json j;
    j["timestamp_start"] = entry.timestamp_start;
    j["timestamp_end"]   = entry.timestamp_end;
    j["request_id"]      = entry.request_id;
    j["client_id"]       = entry.client_id;
    j["sql"]             = entry.sql;
    j["target_node"]     = entry.target_node;
    j["shard_id"]        = entry.shard_id;
    j["duration_ms"]     = entry.duration_ms;
    j["thread_id"]       = entry.thread_id;
    j["status"]          = entry.ok ? "ok" : "error";
    if (!entry.ok) j["error"] = entry.error;

    inst.Push({true, j.dump()});
}

}  
