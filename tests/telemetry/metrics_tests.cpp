#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "telemetry/metrics.h"

TEST(NodeMetricsTest, RecordRequestsRpsBasic) {
    db::NodeMetrics m("test-node", 600);

    
    for (int i = 0; i < 10; ++i) {
        m.RecordRequest(5, false);
    }

    db::MetricsSnapshot snap = m.Snapshot();
    EXPECT_EQ(snap.node_id, "test-node");
    EXPECT_EQ(snap.total_requests, 10u);
    EXPECT_EQ(snap.total_errors, 0u);
    EXPECT_DOUBLE_EQ(snap.error_rate_1min, 0.0);
    
    EXPECT_GE(snap.rps_current, 0.0);
}

TEST(NodeMetricsTest, ErrorRateTracking) {
    db::NodeMetrics m("test-node", 600);

    for (int i = 0; i < 8; ++i) m.RecordRequest(10, false);
    for (int i = 0; i < 2; ++i) m.RecordRequest(10, true);

    db::MetricsSnapshot snap = m.Snapshot();
    EXPECT_EQ(snap.total_requests, 10u);
    EXPECT_EQ(snap.total_errors,   2u);
    
    EXPECT_NEAR(snap.error_rate_1min, 0.2, 0.01);
}

TEST(NodeMetricsTest, LatencyPercentiles) {
    db::NodeMetrics m("test-node", 600);

    
    for (int i = 1; i <= 100; ++i) {
        m.RecordRequest(static_cast<int64_t>(i), false);
    }

    db::MetricsSnapshot snap = m.Snapshot();
    
    EXPECT_NEAR(snap.latency_avg_ms, 50.5, 2.0);
    
    EXPECT_GE(snap.latency_p95_ms, 90.0);
    
    EXPECT_GE(snap.latency_p99_ms, 95.0);
}

TEST(NodeMetricsTest, RpsMaxTracking) {
    db::NodeMetrics m("test-node", 600);

    for (int i = 0; i < 20; ++i) m.RecordRequest(1, false);

    db::MetricsSnapshot snap = m.Snapshot();
    EXPECT_GE(snap.rps_max_10min, 1.0);
}

TEST(NodeMetricsTest, NodeIdPreserved) {
    db::NodeMetrics m("my-special-node", 600);
    EXPECT_EQ(m.Snapshot().node_id, "my-special-node");
}

TEST(ClusterMetricsTest, AggregatesMultipleNodes) {
    db::ClusterMetrics cluster;

    db::MetricsSnapshot s1;
    s1.node_id       = "n1";
    s1.rps_current   = 100.0;
    s1.total_requests = 1000;
    s1.total_errors   = 10;
    s1.latency_p95_ms = 50.0;
    s1.latency_p99_ms = 90.0;

    db::MetricsSnapshot s2;
    s2.node_id        = "n2";
    s2.rps_current    = 200.0;
    s2.total_requests = 2000;
    s2.total_errors   = 20;
    s2.latency_p95_ms = 60.0;
    s2.latency_p99_ms = 100.0;

    cluster.UpdateNode(s1);
    cluster.UpdateNode(s2);

    db::MetricsSnapshot agg = cluster.Aggregate();
    EXPECT_EQ(agg.node_id, "cluster");
    EXPECT_NEAR(agg.rps_current,   300.0, 0.1);
    EXPECT_EQ(agg.total_requests,  3000u);
    EXPECT_EQ(agg.total_errors,    30u);
    
    EXPECT_NEAR(agg.latency_p95_ms, 60.0,  0.1);
    EXPECT_NEAR(agg.latency_p99_ms, 100.0, 0.1);
}

TEST(ClusterMetricsTest, UpdateSameNodeReplaces) {
    db::ClusterMetrics cluster;

    db::MetricsSnapshot s1;
    s1.node_id       = "n1";
    s1.total_requests = 100;

    db::MetricsSnapshot s2;
    s2.node_id        = "n1";
    s2.total_requests = 200;  

    cluster.UpdateNode(s1);
    cluster.UpdateNode(s2);

    db::MetricsSnapshot agg = cluster.Aggregate();
    EXPECT_EQ(agg.total_requests, 200u);
}
