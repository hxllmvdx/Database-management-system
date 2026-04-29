#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include "../execution/value.h"

namespace db {

enum class ColumnType {
    kInt,
    kString,
};

struct ColumnSchema {
    std::string name;
    ColumnType type = ColumnType::kInt;
    bool not_null = false;
    bool indexed = false;
    std::optional<Value> default_value;
};

class TableSchema {
public:
    std::vector<ColumnSchema> columns;

    int FindColumn(const std::string& name) const;
};

}
