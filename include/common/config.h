#pragma once
#include <cstddef>
#include <string>

namespace db {

struct Config {
    std::string data_dir = "./data";
    std::string wal_dir = "./data/wal";
    std::string host = "127.0.0.1";
    int port = 7000;
    std::size_t page_size = 4096;
};

}
