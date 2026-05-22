#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace db {

struct NodeConfig {
    std::string id;    
    std::string host;  
    int         port;  
};

struct Config {
    
    std::string data_dir = "./data";
    std::string wal_dir  = "./data/wal";
    std::size_t page_size = 4096;

    
    std::string host = "127.0.0.1";
    int         port = 7000;

    
    int  entrypoint_port        = 9000;
    int  virtual_nodes_per_node = 150;   

    
    std::vector<NodeConfig> nodes;

    
    std::string task_queue_dir     = "./data/task_queue";
    std::size_t task_queue_workers = 4;
    std::size_t task_queue_max     = 10000;

    
    std::string log_dir      = "./data/logs";
    std::string log_level    = "info";   
    std::size_t log_max_mb   = 32;       
    std::size_t log_max_files = 5;

    
    int telemetry_window_seconds = 600;   
    int telemetry_push_interval  = 5;     

    
    std::string jwt_secret      = "coursedb-secret-change-in-production";
    int         jwt_ttl_seconds = 3600;
    int         pbkdf2_iterations = 260000;
};

}  
