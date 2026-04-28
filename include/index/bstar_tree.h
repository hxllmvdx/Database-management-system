#pragma once
#include <optional>
#include <vector>
#include "../common/bytes.h"
#include "../common/status.h"
#include "../execution/value.h"
#include "../storage/row_id.h"

namespace db {

struct KeyRange {
    std::optional<Value> low;
    std::optional<Value> high;
    bool include_low = true;
    bool include_high = false;
};

class BStarTree {
public:
    BStarTree(std::string index_file, ValueType key_type);

    Status Open();
    Status Insert(const Value& key, RowId rid);
    Status Delete(const Value& key);
    Status Find(const Value& key, std::optional<RowId>* out);
    Status RangeSearch(const KeyRange& range, std::vector<RowId>* out);

private:
    std::string index_file_;
    ValueType key_type_;
};

}
