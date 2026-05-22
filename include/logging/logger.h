#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "../common/config.h"

namespace db {

enum class LogLevel : int {
    kTrace = 0,
    kDebug = 1,
    kInfo  = 2,
    kWarn  = 3,
    kError = 4,
};

struct AccessLogEntry {
    std::string timestamp_start;   
    std::string timestamp_end;     
    std::string request_id;
    std::string client_id;
    std::string sql;
    std::string target_node;
    std::string shard_id;
    int64_t     duration_ms  = 0;
    int         thread_id    = 0;
    bool        ok           = true;
    std::string error;
};

class Logger {
public:
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger() { ShutdownImpl(); }

    
    
    static void Init(const Config& config);

    
    static void Shutdown();

    
    static void Flush();

    static void Trace(const std::string& msg,
                      const std::map<std::string, std::string>& fields = {});
    static void Debug(const std::string& msg,
                      const std::map<std::string, std::string>& fields = {});
    static void Info(const std::string& msg,
                     const std::map<std::string, std::string>& fields = {});
    static void Warn(const std::string& msg,
                     const std::map<std::string, std::string>& fields = {});
    static void Error(const std::string& msg,
                      const std::map<std::string, std::string>& fields = {});

    
    static void AccessLog(const AccessLogEntry& entry);

    
    static std::string NowIso8601();

    
    static int64_t NowMs();

private:
    struct LogEntry {
        bool         is_access = false;
        std::string  json_line;
    };

    static Logger& Instance();
    Logger() = default;

    void    InitImpl(const Config& config);
    void    ShutdownImpl();
    void    FlushImpl();
    void    Push(LogEntry entry);
    void    WriterLoop();
    void    WriteToFile(const std::string& path, const std::string& line);
    void    MaybeRotate(const std::string& path);
    bool    ShouldLog(LogLevel level) const;
    std::string BuildJson(LogLevel level, const std::string& msg,
                          const std::map<std::string, std::string>& fields) const;

    std::string log_dir_;
    std::string server_log_path_;
    std::string access_log_path_;
    std::size_t max_bytes_    = 32ULL * 1024 * 1024;
    std::size_t max_files_    = 5;
    LogLevel    min_level_    = LogLevel::kInfo;

    std::mutex              queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<LogEntry>    queue_;
    std::size_t             max_queue_ = 65536;

    std::atomic<bool> running_{false};
    std::thread       writer_thread_;
    bool              initialized_ = false;
};

}  
