#include "index/index_manager.h"

#include <utility>

#include "common/file_utils.h"

namespace db {

IndexManager::IndexManager(std::size_t page_size, std::size_t max_variable_key_payload_bytes)
    : page_size_(page_size),
      max_variable_key_payload_bytes_(max_variable_key_payload_bytes) {}

Status IndexManager::ValidateDescriptor(const IndexDescriptor& desc) const {
    if (desc.name.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "name must not be empty");
    }
    if (desc.table_name.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "table_name must not be empty");
    }
    if (desc.column_name.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "column_name must not be empty");
    }
    if (desc.file_path.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "file_path must not be empty");
    }
    return Status::Ok();
}

Status IndexManager::ResolveIndexFilePath(const IndexDescriptor& desc, std::string* out_file_path) const {
    if (out_file_path == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_file_path must not be nullptr");
    }

    Status status = ValidateDescriptor(desc);
    if (!status.ok()) {
        return status;
    }

    *out_file_path = desc.file_path;
    return Status::Ok();
}

Status IndexManager::GetOpenedIndex(const std::string& index_name, BStarTree** out_tree) {
    if (out_tree == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_tree must not be nullptr");
    }

    auto it = opened_indexes_.find(index_name);
    if (it == opened_indexes_.end()) {
        return Status::Error(StatusCode::kNotFound, "index is not open");
    }

    *out_tree = it->second.get();
    return Status::Ok();
}

Status IndexManager::RegisterIndex(const IndexDescriptor& desc, ValueType key_type, bool create_if_missing) {
    Status status = ValidateDescriptor(desc);
    if (!status.ok()) {
        return status;
    }

    if (opened_indexes_.find(desc.name) != opened_indexes_.end()) {
        return Status::Ok();
    }

    std::string file_path;
    status = ResolveIndexFilePath(desc, &file_path);
    if (!status.ok()) {
        return status;
    }

    bool exists = false;
    status = file_utils::FileExists(file_path, &exists);
    if (!status.ok()) {
        return status;
    }
    if (!create_if_missing && !exists) {
        return Status::Error(StatusCode::kNotFound, "index file does not exist");
    }

    std::size_t max_encoded_key_size = 0;
    status = BStarTree::ResolveMaxEncodedKeySize(
        key_type, max_variable_key_payload_bytes_, &max_encoded_key_size);
    if (!status.ok()) {
        return status;
    }

    std::size_t min_degree = 0;
    status = BStarTree::ComputeMinDegreeForPage(page_size_, max_encoded_key_size, &min_degree);
    if (!status.ok()) {
        return status;
    }

    auto tree = std::make_unique<BStarTree>(
        file_path, key_type, page_size_, min_degree, max_variable_key_payload_bytes_);
    status = tree->Open();
    if (!status.ok()) {
        return status;
    }

    opened_indexes_.emplace(desc.name, std::move(tree));
    return Status::Ok();
}

Status IndexManager::CreateIndex(const IndexDescriptor& desc, ValueType key_type) {
    return RegisterIndex(desc, key_type, true);
}

Status IndexManager::OpenIndex(const IndexDescriptor& desc, ValueType key_type) {
    return RegisterIndex(desc, key_type, false);
}

Status IndexManager::Insert(const std::string& index_name, const Value& key, RowId rid) {
    BStarTree* tree = nullptr;
    Status status = GetOpenedIndex(index_name, &tree);
    if (!status.ok()) {
        return status;
    }
    return tree->Insert(key, rid);
}

Status IndexManager::Delete(const std::string& index_name, const Value& key) {
    BStarTree* tree = nullptr;
    Status status = GetOpenedIndex(index_name, &tree);
    if (!status.ok()) {
        return status;
    }
    return tree->Delete(key);
}

Status IndexManager::Find(const std::string& index_name, const Value& key, std::optional<RowId>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    BStarTree* tree = nullptr;
    Status status = GetOpenedIndex(index_name, &tree);
    if (!status.ok()) {
        return status;
    }
    return tree->Find(key, out);
}

Status IndexManager::RangeSearch(const std::string& index_name,
                                 const KeyRange& range,
                                 std::vector<RowId>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }

    BStarTree* tree = nullptr;
    Status status = GetOpenedIndex(index_name, &tree);
    if (!status.ok()) {
        return status;
    }
    return tree->RangeSearch(range, out);
}

}  
