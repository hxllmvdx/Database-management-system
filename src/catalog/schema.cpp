#include "catalog/schema.h"

int db::TableSchema::FindColumn(const std::string& name) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
