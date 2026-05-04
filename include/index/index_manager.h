#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

#include "../common/status.h"
#include "../catalog/index_descriptor.h"
#include "bstar_tree.h"

namespace db {

class IndexManager {
public:
    explicit IndexManager(
        std::size_t page_size = 4096,
        std::size_t max_variable_key_payload_bytes = BStarTree::kDefaultMaxVariableKeyPayloadBytes);

    Status CreateIndex(const IndexDescriptor& desc, ValueType key_type);
    Status OpenIndex(const IndexDescriptor& desc, ValueType key_type);
    Status Insert(const std::string& index_name, const Value& key, RowId rid);
    Status Delete(const std::string& index_name, const Value& key);
    Status Find(const std::string& index_name, const Value& key, std::optional<RowId>* out);
    Status RangeSearch(const std::string& index_name, const KeyRange& range, std::vector<RowId>* out);

private:
    Status ValidateDescriptor(const IndexDescriptor& desc) const;
    Status ResolveIndexFilePath(const IndexDescriptor& desc, std::string* out_file_path) const;
    Status RegisterIndex(const IndexDescriptor& desc, ValueType key_type, bool create_if_missing);
    Status GetOpenedIndex(const std::string& index_name, BStarTree** out_tree);

    std::size_t page_size_;
    std::size_t max_variable_key_payload_bytes_;
    std::unordered_map<std::string, std::unique_ptr<BStarTree>> opened_indexes_;
};

}
