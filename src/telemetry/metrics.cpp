#include "telemetry/metrics.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>

namespace db {

static int64_t NowSec() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

NodeMetrics::NodeMetrics(std::string node_id, int window_seconds)
    : node_id_(std::move(node_id)), window_seconds_(window_seconds) {}

void NodeMetrics::AdvanceBuckets(int64_t now_sec) const {
    
    if (buckets_.empty() || buckets_.back().second_ts < now_sec) {
        buckets_.push_back({now_sec, 0, 0});
    }
    
    const int64_t cutoff = now_sec - static_cast<int64_t>(window_seconds_);
    while (!buckets_.empty() && buckets_.front().second_ts <= cutoff) {
        buckets_.pop_front();
    }
}

void NodeMetrics::RecordRequest(int64_t duration_ms, bool is_error) {
    total_requests_.fetch_add(1, std::memory_order_relaxed);
    if (is_error) {
        total_errors_.fetch_add(1, std::memory_order_relaxed);
    }

    const int64_t now_sec = NowSec();
    const int64_t now_ms  = NowMs();

    std::unique_lock<std::mutex> lock(mu_);

    AdvanceBuckets(now_sec);
    buckets_.back().count++;
    if (is_error) buckets_.back().errors++;

    
    latency_buf_.push_back(duration_ms);
    latency_ts_.push_back(now_ms);

    const int64_t lat_cutoff_ms =
        now_ms - static_cast<int64_t>(latency_window_seconds_) * 1000;
    while (!latency_ts_.empty() && latency_ts_.front() < lat_cutoff_ms) {
        latency_ts_.pop_front();
        latency_buf_.pop_front();
    }
}

MetricsSnapshot NodeMetrics::Snapshot() const {
    const int64_t now_sec = NowSec();
    const int64_t now_ms  = NowMs();

    std::unique_lock<std::mutex> lock(mu_);
    AdvanceBuckets(now_sec);

    MetricsSnapshot snap;
    snap.node_id           = node_id_;
    snap.total_requests    = total_requests_.load(std::memory_order_relaxed);
    snap.total_errors      = total_errors_.load(std::memory_order_relaxed);
    snap.snapshot_timestamp = now_ms;

    
    
    uint64_t last_sec_count = 0;
    if (!buckets_.empty()) {
        last_sec_count = buckets_.back().count;
    }
    snap.rps_current = static_cast<double>(last_sec_count);

    
    if (!buckets_.empty()) {
        const int64_t window_actual =
            std::max<int64_t>(1, now_sec - buckets_.front().second_ts + 1);
        uint64_t total_in_window = 0;
        uint64_t max_in_sec      = 0;
        for (const auto& b : buckets_) {
            total_in_window += b.count;
            if (b.count > max_in_sec) max_in_sec = b.count;
        }
        snap.rps_avg_10min = static_cast<double>(total_in_window) /
                             static_cast<double>(window_actual);
        snap.rps_max_10min = static_cast<double>(max_in_sec);
    }

    
    
    const int64_t lat_cutoff_ms =
        now_ms - static_cast<int64_t>(latency_window_seconds_) * 1000;
    while (!latency_ts_.empty() && latency_ts_.front() < lat_cutoff_ms) {
        latency_ts_.pop_front();
        latency_buf_.pop_front();
    }

    if (!latency_buf_.empty()) {
        std::vector<int64_t> sorted(latency_buf_.begin(), latency_buf_.end());
        std::sort(sorted.begin(), sorted.end());

        const double sum = static_cast<double>(
            std::accumulate(sorted.begin(), sorted.end(), int64_t{0}));
        snap.latency_avg_ms = sum / static_cast<double>(sorted.size());

        const auto pct = [&](double p) -> double {
            const std::size_t idx =
                static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
            return static_cast<double>(sorted[idx]);
        };
        snap.latency_p95_ms = pct(0.95);
        snap.latency_p99_ms = pct(0.99);
    }

    
    const int64_t one_min_cutoff = now_sec - 60;
    uint64_t      errors_1min    = 0;
    uint64_t      total_1min     = 0;
    for (const auto& b : buckets_) {
        if (b.second_ts >= one_min_cutoff) {
            errors_1min += b.errors;
            total_1min  += b.count;
        }
    }
    snap.error_rate_1min =
        (total_1min > 0)
            ? static_cast<double>(errors_1min) / static_cast<double>(total_1min)
            : 0.0;

    return snap;
}

void ClusterMetrics::UpdateNode(const MetricsSnapshot& snap) {
    std::unique_lock<std::mutex> lock(mu_);
    for (auto& existing : node_snapshots_) {
        if (existing.node_id == snap.node_id) {
            existing = snap;
            return;
        }
    }
    node_snapshots_.push_back(snap);
}

MetricsSnapshot ClusterMetrics::Aggregate() const {
    std::unique_lock<std::mutex> lock(mu_);
    MetricsSnapshot agg;
    agg.node_id = "cluster";

    if (node_snapshots_.empty()) return agg;

    for (const auto& s : node_snapshots_) {
        agg.rps_current      += s.rps_current;
        agg.rps_avg_10min    += s.rps_avg_10min;
        agg.rps_max_10min    += s.rps_max_10min;
        agg.latency_avg_ms   += s.latency_avg_ms;
        agg.latency_p95_ms   =  std::max(agg.latency_p95_ms, s.latency_p95_ms);
        agg.latency_p99_ms   =  std::max(agg.latency_p99_ms, s.latency_p99_ms);
        agg.error_rate_1min  += s.error_rate_1min;
        agg.total_requests   += s.total_requests;
        agg.total_errors     += s.total_errors;
    }

    const double n = static_cast<double>(node_snapshots_.size());
    agg.latency_avg_ms  /= n;
    agg.error_rate_1min /= n;

    return agg;
}

}  
