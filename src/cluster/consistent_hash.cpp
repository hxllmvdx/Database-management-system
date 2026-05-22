#include "cluster/consistent_hash.h"

#include <algorithm>
#include <cstdint>
#include <set>

namespace db {

static inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

static uint32_t MurmurHash3_x86_32(const void* key, int len, uint32_t seed) {
    const uint8_t* data    = static_cast<const uint8_t*>(key);
    const int      nblocks = len / 4;

    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51u;
    const uint32_t c2 = 0x1b873593u;

    const uint32_t* blocks =
        reinterpret_cast<const uint32_t*>(data + nblocks * 4);

    for (int i = -nblocks; i; ++i) {
        uint32_t k1 = blocks[i];
        k1 *= c1;
        k1  = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1  = rotl32(h1, 13);
        h1  = h1 * 5 + 0xe6546b64u;
    }

    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
        case 2: k1 ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1  = rotl32(k1, 15);
                k1 *= c2;
                h1 ^= k1;
                break;
    }

    h1 ^= static_cast<uint32_t>(len);
    
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6bu;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35u;
    h1 ^= h1 >> 16;

    return h1;
}

ConsistentHashRing::ConsistentHashRing(int virtual_nodes)
    : virtual_nodes_(virtual_nodes) {}

uint32_t ConsistentHashRing::Hash(const std::string& key) {
    return MurmurHash3_x86_32(key.data(), static_cast<int>(key.size()), 0x9747b28cu);
}

void ConsistentHashRing::AddNode(const std::string& node_id) {
    for (int i = 0; i < virtual_nodes_; ++i) {
        const std::string vnode_key = node_id + "#" + std::to_string(i);
        ring_[Hash(vnode_key)] = node_id;
    }
}

void ConsistentHashRing::RemoveNode(const std::string& node_id) {
    for (int i = 0; i < virtual_nodes_; ++i) {
        const std::string vnode_key = node_id + "#" + std::to_string(i);
        ring_.erase(Hash(vnode_key));
    }
}

std::string ConsistentHashRing::GetNode(const std::string& key) const {
    if (ring_.empty()) return {};

    const uint32_t h  = Hash(key);
    auto           it = ring_.lower_bound(h);
    if (it == ring_.end()) {
        it = ring_.begin();  
    }
    return it->second;
}

std::vector<std::string> ConsistentHashRing::AllNodes() const {
    std::set<std::string> unique;
    for (const auto& [hash, node] : ring_) {
        unique.insert(node);
    }
    return {unique.begin(), unique.end()};
}

}  
