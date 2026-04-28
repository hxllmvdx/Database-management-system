#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../common/status.h"
#include "../catalog/index_descriptor.h"
#include "bstar_tree.h"

namespace db {

class IndexManager {
public:
    IndexManager();
    ~IndexManager();

    IndexManager(IndexManager&&) noexcept;
    IndexManager& operator=(IndexManager&&) noexcept;

    IndexManager(const IndexManager&) = delete;
    IndexManager& operator=(const IndexManager&) = delete;

    Status CreateIndex(const IndexDescriptor& desc, ValueType key_type);
    Status OpenIndex(const IndexDescriptor& desc, ValueType key_type);
    Status Insert(const std::string& index_name, const Value& key, RowId rid);
    Status Delete(const std::string& index_name, const Value& key);
    Status Find(const std::string& index_name, const Value& key, std::optional<RowId>* out);
    Status RangeSearch(const std::string& index_name, const KeyRange& range, std::vector<RowId>* out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
