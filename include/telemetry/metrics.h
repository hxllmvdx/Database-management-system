#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace db {

struct MetricsSnapshot {
    std::string node_id;
    double      rps_current         = 0.0;  
    double      rps_avg_10min       = 0.0;  
    double      rps_max_10min       = 0.0;  
    double      latency_avg_ms      = 0.0;  
    double      latency_p95_ms      = 0.0;  
    double      latency_p99_ms      = 0.0;  
    double      error_rate_1min     = 0.0;  
    uint64_t    total_requests      = 0;
    uint64_t    total_errors        = 0;
    int64_t     snapshot_timestamp  = 0;    
};

class NodeMetrics {
public:
    explicit NodeMetrics(std::string node_id,
                         int window_seconds = 600);  

    
    
    
    void RecordRequest(int64_t duration_ms, bool is_error);

    
    MetricsSnapshot Snapshot() const;

    const std::string& node_id() const { return node_id_; }

private:
    struct SecondBucket {
        int64_t  second_ts  = 0;   
        uint64_t count      = 0;
        uint64_t errors     = 0;
    };

    void AdvanceBuckets(int64_t now_sec) const;

    std::string node_id_;
    int         window_seconds_;
    int         latency_window_seconds_ = 10;

    
    mutable std::mutex                mu_;
    mutable std::deque<SecondBucket>  buckets_;      
    mutable std::deque<int64_t>       latency_buf_;  
    mutable std::deque<int64_t>       latency_ts_;   

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_errors_{0};
};

class ClusterMetrics {
public:
    void UpdateNode(const MetricsSnapshot& snap);
    MetricsSnapshot Aggregate() const;  

private:
    mutable std::mutex                           mu_;
    std::vector<MetricsSnapshot>                 node_snapshots_;
};

}  
