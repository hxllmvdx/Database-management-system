#include <gtest/gtest.h>
#include <set>
#include <unordered_map>

#include "cluster/consistent_hash.h"

TEST(ConsistentHashTest, EmptyRingReturnsEmpty) {
    db::ConsistentHashRing ring(10);
    EXPECT_TRUE(ring.GetNode("any-key").empty());
    EXPECT_TRUE(ring.empty());
}

TEST(ConsistentHashTest, SingleNodeAlwaysSelected) {
    db::ConsistentHashRing ring(10);
    ring.AddNode("node-1");

    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(ring.GetNode("key-" + std::to_string(i)), "node-1");
    }
}

TEST(ConsistentHashTest, AllNodesAreMapped) {
    db::ConsistentHashRing ring(150);
    ring.AddNode("node-1");
    ring.AddNode("node-2");
    ring.AddNode("node-3");

    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        seen.insert(ring.GetNode("key-" + std::to_string(i)));
    }
    EXPECT_EQ(seen.size(), 3u);
}

TEST(ConsistentHashTest, RemoveNodeDoesNotReturnIt) {
    db::ConsistentHashRing ring(150);
    ring.AddNode("node-1");
    ring.AddNode("node-2");
    ring.RemoveNode("node-1");

    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(ring.GetNode("key-" + std::to_string(i)), "node-2");
    }
}

TEST(ConsistentHashTest, AllNodesListed) {
    db::ConsistentHashRing ring(10);
    ring.AddNode("a");
    ring.AddNode("b");
    ring.AddNode("c");

    const auto nodes = ring.AllNodes();
    const std::set<std::string> node_set(nodes.begin(), nodes.end());
    EXPECT_EQ(node_set.count("a"), 1u);
    EXPECT_EQ(node_set.count("b"), 1u);
    EXPECT_EQ(node_set.count("c"), 1u);
}

TEST(ConsistentHashTest, VirtualNodesDistributeLoad) {
    
    db::ConsistentHashRing ring(150);
    ring.AddNode("n1");
    ring.AddNode("n2");
    ring.AddNode("n3");

    std::unordered_map<std::string, int> counts;
    constexpr int kKeys = 9000;
    for (int i = 0; i < kKeys; ++i) {
        counts[ring.GetNode("key" + std::to_string(i))]++;
    }

    
    for (const auto& [node, count] : counts) {
        const double fraction = static_cast<double>(count) / kKeys;
        EXPECT_GT(fraction, 0.20) << "node " << node << " is starved";
        EXPECT_LT(fraction, 0.47) << "node " << node << " is overloaded";
    }
}

TEST(ConsistentHashTest, MinimalRemapOnNodeAddition) {
    db::ConsistentHashRing ring(150);
    ring.AddNode("n1");
    ring.AddNode("n2");

    constexpr int kKeys = 1000;
    std::unordered_map<std::string, std::string> before;
    for (int i = 0; i < kKeys; ++i) {
        const std::string key = "key" + std::to_string(i);
        before[key] = ring.GetNode(key);
    }

    ring.AddNode("n3");

    int remapped = 0;
    for (int i = 0; i < kKeys; ++i) {
        const std::string key = "key" + std::to_string(i);
        if (ring.GetNode(key) != before[key]) remapped++;
    }

    
    const double remap_rate = static_cast<double>(remapped) / kKeys;
    EXPECT_GT(remap_rate, 0.15);
    EXPECT_LT(remap_rate, 0.55);
}

TEST(ConsistentHashTest, DeterministicMapping) {
    db::ConsistentHashRing ring1(150);
    db::ConsistentHashRing ring2(150);

    ring1.AddNode("a"); ring1.AddNode("b");
    ring2.AddNode("a"); ring2.AddNode("b");

    for (int i = 0; i < 500; ++i) {
        const std::string key = "k" + std::to_string(i);
        EXPECT_EQ(ring1.GetNode(key), ring2.GetNode(key));
    }
}
