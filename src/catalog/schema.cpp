#include "catalog/schema.h"

int db::TableSchema::FindColumn(const std::string& name) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const db::ColumnSchema* db::TableSchema::FindColumnSchema(const std::string& name) const {
    const int index = FindColumn(name);
    if (index < 0) {
        return nullptr;
    }
    return &columns[static_cast<std::size_t>(index)];
}

db::ColumnSchema* db::TableSchema::FindColumnSchema(const std::string& name) {
    const int index = FindColumn(name);
    if (index < 0) {
        return nullptr;
    }
    return &columns[static_cast<std::size_t>(index)];
}
