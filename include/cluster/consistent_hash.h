#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace db {

class ConsistentHashRing {
public:
    explicit ConsistentHashRing(int virtual_nodes = 150);

    
    void AddNode(const std::string& node_id);

    
    void RemoveNode(const std::string& node_id);

    
    
    std::string GetNode(const std::string& key) const;

    
    std::vector<std::string> AllNodes() const;

    std::size_t size() const { return ring_.size(); }
    bool empty() const { return ring_.empty(); }

private:
    static uint32_t Hash(const std::string& key);

    int                           virtual_nodes_;
    std::map<uint32_t, std::string> ring_;  
};

}  
