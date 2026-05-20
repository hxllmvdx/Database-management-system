#pragma once
#include <cstdint>

namespace db {

struct PageId {
    std::uint64_t value = 0;

    bool operator==(const PageId& other) const { return value == other.value; }
    bool operator!=(const PageId& other) const { return value != other.value; }
};

}
