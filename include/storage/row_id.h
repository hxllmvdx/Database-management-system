#pragma once
#include <cstdint>

namespace db {

struct RowId {
    std::uint64_t value = 0;

    bool operator==(const RowId& other) const { return value == other.value; }
    bool operator!=(const RowId& other) const { return value != other.value; }
};

}
