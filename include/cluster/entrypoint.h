#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../cluster/consistent_hash.h"
#include "../common/config.h"
#include "../common/status.h"
#include "../network/request.h"
#include "../network/response.h"
#include "../network/tcp_server.h"
#include "../telemetry/metrics.h"

namespace db {

enum class NodeHealth { kUnknown, kHealthy, kUnhealthy };

struct NodeState {
    NodeConfig    config;
    NodeHealth    health          = NodeHealth::kUnknown;
    int64_t       last_seen_ms   = 0;
    MetricsSnapshot metrics;
};

class EntrypointNode {
public:
    explicit EntrypointNode(Config config);
    ~EntrypointNode();

    
    Status Start();

    
    Status Stop();

    
    Status AddNode(const NodeConfig& node);

    
    Status RemoveNode(const std::string& node_id);

    
    MetricsSnapshot ClusterMetricsSnapshot() const;

private:
    
    Response HandleRequest(const Session& session, const Request& req);
    Response ForwardToNode(const NodeState& node, const Request& req);
    Response HandleManagement(const Request& req);

    
    std::string ShardKey(const Request& req) const;
    const NodeState* ResolveNode(const std::string& shard_key) const;

    
    void HealthCheckLoop();

    
    void MetricsAggregationLoop();

    Config               config_;
    TcpServer            server_;
    ConsistentHashRing   ring_;
    ClusterMetrics       cluster_metrics_;

    mutable std::mutex                               nodes_mu_;
    std::unordered_map<std::string, NodeState>       nodes_;

    std::atomic<bool>    running_{false};
    std::thread          health_thread_;
    std::thread          metrics_thread_;

    static constexpr int kHealthCheckIntervalMs = 5000;
    static constexpr int kHeartbeatTimeoutMs    = 15000;
};

}  
