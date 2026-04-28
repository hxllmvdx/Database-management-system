#pragma once
#include <cstdint>
#include <string>
#include "../execution/tuple.h"
#include "../storage/row_id.h"

namespace db {

enum class VersionOp {
    kInsert,
    kUpdate,
    kDelete,
};

struct VersionRecord {
    std::int64_t timestamp_ms = 0;
    std::string table_name;
    RowId rid;
    VersionOp op = VersionOp::kInsert;
    Tuple before;
    Tuple after;
};

}
