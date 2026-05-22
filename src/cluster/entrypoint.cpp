#include "cluster/entrypoint.h"

#include <chrono>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "logging/logger.h"
#include "network/tcp_client.h"
#include "network/protocol.h"

using json = nlohmann::json;

namespace db {

static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static Response ErrorResponse(const std::string& msg) {
    Response resp;
    resp.ok    = false;
    resp.error = msg;
    return resp;
}

EntrypointNode::EntrypointNode(Config config)
    : config_(std::move(config))
    , server_(config_.host, config_.entrypoint_port)
    , ring_(config_.virtual_nodes_per_node) {

    
    for (const auto& nc : config_.nodes) {
        NodeState state;
        state.config      = nc;
        state.health      = NodeHealth::kUnknown;
        state.last_seen_ms = 0;
        nodes_[nc.id] = state;
        ring_.AddNode(nc.id);
    }
}

EntrypointNode::~EntrypointNode() {
    Stop();
}

Status EntrypointNode::Start() {
    running_ = true;

    health_thread_ = std::thread(&EntrypointNode::HealthCheckLoop, this);
    metrics_thread_ = std::thread(&EntrypointNode::MetricsAggregationLoop, this);

    const Status s = server_.Start([this](const Session& sess, const Request& req) {
        return HandleRequest(sess, req);
    });
    if (!s.ok()) {
        running_ = false;
        return s;
    }

    Logger::Info("entrypoint started",
                 {{"host", config_.host},
                  {"port", std::to_string(config_.entrypoint_port)},
                  {"nodes", std::to_string(nodes_.size())}});
    return Status::Ok();
}

Status EntrypointNode::Stop() {
    if (!running_.load()) return Status::Ok();
    running_ = false;

    server_.Stop();

    if (health_thread_.joinable()) health_thread_.join();
    if (metrics_thread_.joinable()) metrics_thread_.join();

    Logger::Info("entrypoint stopped");
    return Status::Ok();
}

Status EntrypointNode::AddNode(const NodeConfig& nc) {
    std::unique_lock<std::mutex> lock(nodes_mu_);
    if (nodes_.count(nc.id)) {
        return Status::Error(StatusCode::kAlreadyExists,
                             "node already registered: " + nc.id);
    }
    NodeState state;
    state.config      = nc;
    state.health      = NodeHealth::kUnknown;
    state.last_seen_ms = 0;
    nodes_[nc.id] = state;
    ring_.AddNode(nc.id);

    Logger::Info("cluster node added",
                 {{"node_id", nc.id},
                  {"host", nc.host},
                  {"port", std::to_string(nc.port)}});
    return Status::Ok();
}

Status EntrypointNode::RemoveNode(const std::string& node_id) {
    std::unique_lock<std::mutex> lock(nodes_mu_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return Status::Error(StatusCode::kNotFound,
                             "node not found: " + node_id);
    }
    nodes_.erase(it);
    ring_.RemoveNode(node_id);

    Logger::Info("cluster node removed", {{"node_id", node_id}});
    return Status::Ok();
}

Response EntrypointNode::HandleRequest(const Session& session,
                                        const Request& req) {
    const int64_t start_ms = NowMs();

    
    if (req.sql.find("SHOW METRICS") != std::string::npos ||
        req.sql.find("ADD NODE")     != std::string::npos ||
        req.sql.find("REMOVE NODE")  != std::string::npos) {
        return HandleManagement(req);
    }

    
    const std::string shard_key = ShardKey(req);
    const NodeState* node = nullptr;
    {
        std::unique_lock<std::mutex> lock(nodes_mu_);
        node = ResolveNode(shard_key);
        if (node == nullptr) {
            return ErrorResponse("no healthy storage nodes available");
        }
        
        const NodeState node_copy = *node;
        lock.unlock();

        AccessLogEntry entry;
        entry.timestamp_start = Logger::NowIso8601();
        entry.request_id = req.request_id;
        entry.client_id  = session.client_id;
        entry.sql        = req.sql;
        entry.target_node = node_copy.config.id;
        entry.shard_id    = shard_key;

        Response resp = ForwardToNode(node_copy, req);

        entry.timestamp_end = Logger::NowIso8601();
        entry.duration_ms   = NowMs() - start_ms;
        entry.ok            = resp.ok;
        entry.error         = resp.error;
        Logger::AccessLog(entry);

        
        {
            std::unique_lock<std::mutex> lock2(nodes_mu_);
            auto it = nodes_.find(node_copy.config.id);
            if (it != nodes_.end()) {
                it->second.last_seen_ms = NowMs();
            }
        }

        return resp;
    }
}

Response EntrypointNode::ForwardToNode(const NodeState& node,
                                        const Request&   req) {
    TcpClient client(node.config.host, node.config.port);
    Response  resp;
    const Status s = client.Send(req, &resp);
    if (!s.ok()) {
        Logger::Error("forward to node failed",
                      {{"node_id", node.config.id}, {"error", s.message()}});
        return ErrorResponse("storage node unreachable: " + node.config.id +
                             " - " + s.message());
    }
    return resp;
}

Response EntrypointNode::HandleManagement(const Request& req) {
    if (req.sql.find("SHOW METRICS") != std::string::npos) {
        const MetricsSnapshot agg = ClusterMetricsSnapshot();
        json j;
        j["node_id"]          = agg.node_id;
        j["rps_current"]      = agg.rps_current;
        j["rps_avg_10min"]    = agg.rps_avg_10min;
        j["rps_max_10min"]    = agg.rps_max_10min;
        j["latency_avg_ms"]   = agg.latency_avg_ms;
        j["latency_p95_ms"]   = agg.latency_p95_ms;
        j["latency_p99_ms"]   = agg.latency_p99_ms;
        j["error_rate_1min"]  = agg.error_rate_1min;
        j["total_requests"]   = agg.total_requests;
        j["total_errors"]     = agg.total_errors;

        Response resp;
        resp.ok           = true;
        resp.result.ok    = true;
        resp.result.columns = {"metrics"};
        Tuple row;
        row.values.push_back(Value::String(j.dump(2)));
        resp.result.rows.push_back(std::move(row));
        return resp;
    }
    return ErrorResponse("unsupported management command");
}

std::string EntrypointNode::ShardKey(const Request& req) const {
    return req.database.empty() ? req.sql.substr(0, 64) : req.database;
}

const NodeState* EntrypointNode::ResolveNode(const std::string& shard_key) const {
    
    const std::string node_id = ring_.GetNode(shard_key);
    if (node_id.empty()) return nullptr;
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return nullptr;
    if (it->second.health == NodeHealth::kUnhealthy) {
        
        const auto& all = ring_.AllNodes();
        for (const auto& nid : all) {
            if (nid == node_id) continue;
            auto fit = nodes_.find(nid);
            if (fit != nodes_.end() &&
                fit->second.health != NodeHealth::kUnhealthy) {
                return &fit->second;
            }
        }
        return nullptr;
    }
    return &it->second;
}

void EntrypointNode::HealthCheckLoop() {
    while (running_.load()) {
        std::vector<std::pair<std::string, NodeConfig>> snapshot;
        {
            std::unique_lock<std::mutex> lock(nodes_mu_);
            for (const auto& [id, state] : nodes_) {
                snapshot.emplace_back(id, state.config);
            }
        }

        for (const auto& [id, cfg] : snapshot) {
            
            Request ping;
            ping.request_id = "ping";
            ping.sql        = "SELECT 1;";
            Response resp;
            TcpClient client(cfg.host, cfg.port);
            const bool alive = client.Send(ping, &resp).ok();

            std::unique_lock<std::mutex> lock(nodes_mu_);
            auto it = nodes_.find(id);
            if (it == nodes_.end()) continue;

            const NodeHealth prev = it->second.health;
            if (alive) {
                it->second.health       = NodeHealth::kHealthy;
                it->second.last_seen_ms = NowMs();
                if (prev == NodeHealth::kUnhealthy) {
                    Logger::Warn("node recovered", {{"node_id", id}});
                }
            } else {
                it->second.health = NodeHealth::kUnhealthy;
                if (prev != NodeHealth::kUnhealthy) {
                    Logger::Error("node failure detected", {{"node_id", id}});
                }
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(kHealthCheckIntervalMs));
    }
}

void EntrypointNode::MetricsAggregationLoop() {
    while (running_.load()) {
        
        
        
        std::this_thread::sleep_for(
            std::chrono::seconds(config_.telemetry_push_interval));
    }
}

MetricsSnapshot EntrypointNode::ClusterMetricsSnapshot() const {
    return cluster_metrics_.Aggregate();
}

}  
