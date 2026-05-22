#include "index/bstar_tree.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

#include "common/binary_io.h"
#include "index/key_encoder.h"

namespace {

constexpr std::array<char, 8> kTreeMetadataMagic = {'C', 'D', 'B', 'B', '*', '+', 'M', 'T'};
constexpr std::array<char, 8> kTreeNodeMagic = {'C', 'D', 'B', 'B', '*', '+', 'N', 'D'};
constexpr std::uint32_t kTreeVersion = 3;
constexpr std::uint64_t kInvalidPageIdValue = std::numeric_limits<std::uint64_t>::max();

db::PageId InvalidPageId() {
    return db::PageId{kInvalidPageIdValue};
}

bool IsInvalidPageId(db::PageId page_id) {
    return page_id.value == kInvalidPageIdValue;
}

std::size_t MetadataHeaderSize() {
    return kTreeMetadataMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint32_t) +
           sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) +
           sizeof(std::uint64_t) +
           sizeof(std::uint64_t);
}

std::size_t NodeHeaderSize() {
    return kTreeNodeMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint32_t) +
           sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint8_t);
}

bool FitsInRange(const db::Value& key, const db::KeyRange& range) {
    if (range.low.has_value()) {
        if (range.include_low) {
            if (key < *range.low) {
                return false;
            }
        } else if (!(key > *range.low)) {
            return false;
        }
    }

    if (range.high.has_value()) {
        if (range.include_high) {
            if (key > *range.high) {
                return false;
            }
        } else if (!(key < *range.high)) {
            return false;
        }
    }

    return true;
}

std::optional<std::size_t> ChooseBalancedSplit(
    std::size_t total_size,
    const std::function<bool(std::size_t, std::size_t)>& fits) {
    std::optional<std::size_t> best_index;
    std::size_t best_distance = std::numeric_limits<std::size_t>::max();
    for (std::size_t split = 1; split < total_size; ++split) {
        const std::size_t left_count = split;
        const std::size_t right_count = total_size - split;
        if (!fits(left_count, right_count)) {
            continue;
        }
        const std::size_t distance = left_count > right_count ? left_count - right_count : right_count - left_count;
        if (!best_index.has_value() || distance < best_distance) {
            best_index = split;
            best_distance = distance;
        }
    }
    return best_index;
}

}  

namespace db {

Status BStarTree::ResolveMaxEncodedKeySize(ValueType key_type,
                                           std::size_t max_variable_key_payload_bytes,
                                           std::size_t* out_max_encoded_key_size) {
    if (out_max_encoded_key_size == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "out_max_encoded_key_size must not be nullptr");
    }
    if (max_variable_key_payload_bytes == 0U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "max_variable_key_payload_bytes must be greater than zero");
    }

    switch (key_type) {
    case ValueType::kInt:
        *out_max_encoded_key_size = sizeof(std::int64_t);
        return Status::Ok();
    case ValueType::kBool:
        *out_max_encoded_key_size = 1U;
        return Status::Ok();
    case ValueType::kString:
        *out_max_encoded_key_size = max_variable_key_payload_bytes;
        return Status::Ok();
    case ValueType::kNull:
        return Status::Error(StatusCode::kInvalidArgument,
                             "NULL is not a valid BStarTree key type");
    }

    return Status::Error(StatusCode::kInvalidArgument, "Unsupported BStarTree key type");
}

Status BStarTree::ComputeMinDegreeForPage(std::size_t page_size,
                                          std::size_t max_encoded_key_size,
                                          std::size_t* out_min_degree) {
    if (out_min_degree == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_min_degree must not be nullptr");
    }
    if (max_encoded_key_size == 0U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "max_encoded_key_size must be greater than zero");
    }

    const std::size_t header_size = NodeHeaderSize();
    const std::size_t leaf_entry_size =
        sizeof(std::uint32_t) + max_encoded_key_size + sizeof(std::uint64_t);
    const std::size_t internal_bundle_size =
        sizeof(std::uint32_t) + max_encoded_key_size + sizeof(std::uint64_t);

    if (page_size <= header_size + sizeof(std::uint64_t)) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Page size is too small for BStarTree node headers");
    }

    const std::size_t leaf_max_keys = (page_size - header_size) / leaf_entry_size;
    const std::size_t internal_max_keys =
        (page_size - header_size - sizeof(std::uint64_t)) / internal_bundle_size;
    const std::size_t max_keys = std::min(leaf_max_keys, internal_max_keys);
    if (max_keys < 3U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Page size is too small to support BStarTree min_degree >= 2");
    }

    *out_min_degree = (max_keys + 1U) / 2U;
    return Status::Ok();
}

BStarTree::BStarTree(std::string index_file,
                     ValueType key_type,
                     std::size_t page_size,
                     std::size_t min_degree,
                     std::size_t max_variable_key_payload_bytes)
    : index_file_(std::move(index_file)),
      key_type_(key_type),
      page_size_(page_size),
      min_degree_(min_degree),
      max_key_payload_bytes_(0),
      page_manager_(index_file_, page_size_) {
    std::size_t resolved_max_key_payload_bytes = 0;
    if (ResolveMaxEncodedKeySize(key_type_, max_variable_key_payload_bytes,
                                 &resolved_max_key_payload_bytes)
            .ok()) {
        max_key_payload_bytes_ = resolved_max_key_payload_bytes;
    }
    if (min_degree_ == 0U && max_key_payload_bytes_ != 0U) {
        std::size_t computed_min_degree = 0;
        if (ComputeMinDegreeForPage(page_size_, max_key_payload_bytes_, &computed_min_degree).ok()) {
            min_degree_ = computed_min_degree;
        }
    }

    metadata_.version = kTreeVersion;
    metadata_.root_page_id = InvalidPageId();
    metadata_.first_leaf_page_id = InvalidPageId();
    metadata_.min_degree = min_degree_;
    metadata_.max_key_payload_bytes = max_key_payload_bytes_;
    metadata_.tree_height = 0;
}

Status BStarTree::Open() {
    if (is_open_) {
        return Status::Ok();
    }
    if (index_file_.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "Index file path is empty");
    }
    if (max_key_payload_bytes_ == 0U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "BStarTree key payload policy must be greater than zero");
    }
    if (min_degree_ < 2U) {
        return Status::Error(StatusCode::kInvalidArgument, "BStarTree min_degree must be at least 2");
    }

    Status status = page_manager_.Open();
    if (!status.ok()) {
        return status;
    }

    Page metadata_page;
    status = page_manager_.ReadPage(PageId{0}, &metadata_page);
    if (!status.ok()) {
        if (status.code() != StatusCode::kNotFound) {
            return status;
        }
        status = InitializeTree();
        if (!status.ok()) {
            return status;
        }
    } else {
        status = LoadMetadata();
        if (!status.ok()) {
            return status;
        }
    }

    is_open_ = true;
    return Status::Ok();
}

Status BStarTree::Insert(const Value& key, RowId rid) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "BStarTree is not open");
    }

    Status status = ValidateKeyType(key);
    if (!status.ok()) {
        return status;
    }

    if (IsInvalidPageId(metadata_.root_page_id)) {
        PageId root_page_id;
        status = AllocateNodePage(&root_page_id);
        if (!status.ok()) {
            return status;
        }

        NodeHeader root_header;
        root_header.type = NodeType::kLeaf;
        root_header.parent_page_id = InvalidPageId();
        root_header.next_leaf_page_id = InvalidPageId();
        root_header.is_root = true;

        LeafNodeData leaf;
        leaf.entries.push_back(LeafEntry{key, rid});

        Page root_page;
        status = BuildLeafPage(root_page_id, root_header, leaf, &root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(root_page);
        if (!status.ok()) {
            return status;
        }

        metadata_.root_page_id = root_page_id;
        metadata_.first_leaf_page_id = root_page_id;
        metadata_.tree_height = 1;
        return WriteMetadata();
    }

    std::vector<SearchPathEntry> path;
    PageId leaf_page_id = InvalidPageId();
    std::optional<RowId> existing_rid;
    status = FindPathToLeaf(key, &path, &leaf_page_id, &existing_rid);
    if (!status.ok()) {
        return status;
    }
    if (existing_rid.has_value()) {
        return Status::Error(StatusCode::kAlreadyExists, "Index key already exists");
    }

    return InsertIntoLeaf(leaf_page_id, path, key, rid);
}

Status BStarTree::Delete(const Value& key) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "BStarTree is not open");
    }

    Status status = ValidateKeyType(key);
    if (!status.ok()) {
        return status;
    }
    if (IsInvalidPageId(metadata_.root_page_id)) {
        return Status::Error(StatusCode::kNotFound, "Index key not found");
    }

    std::vector<SearchPathEntry> path;
    PageId leaf_page_id = InvalidPageId();
    std::optional<RowId> existing_rid;
    status = FindPathToLeaf(key, &path, &leaf_page_id, &existing_rid);
    if (!status.ok()) {
        return status;
    }
    if (!existing_rid.has_value()) {
        return Status::Error(StatusCode::kNotFound, "Index key not found");
    }

    return DeleteFromLeaf(leaf_page_id, path, key);
}

Status BStarTree::Find(const Value& key, std::optional<RowId>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "BStarTree is not open");
    }

    Status status = ValidateKeyType(key);
    if (!status.ok()) {
        return status;
    }

    *out = std::nullopt;
    if (IsInvalidPageId(metadata_.root_page_id)) {
        return Status::Ok();
    }

    std::vector<SearchPathEntry> path;
    PageId leaf_page_id = InvalidPageId();
    std::optional<RowId> existing_rid;
    status = FindPathToLeaf(key, &path, &leaf_page_id, &existing_rid);
    if (!status.ok()) {
        return status;
    }
    *out = existing_rid;
    return Status::Ok();
}

Status BStarTree::RangeSearch(const KeyRange& range, std::vector<RowId>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "BStarTree is not open");
    }

    Status status = ValidateRange(range);
    if (!status.ok()) {
        return status;
    }

    out->clear();
    if (IsInvalidPageId(metadata_.root_page_id)) {
        return Status::Ok();
    }

    PageId current_leaf_page_id = metadata_.first_leaf_page_id;
    if (range.low.has_value()) {
        std::vector<SearchPathEntry> path;
        std::optional<RowId> existing_rid;
        status = FindPathToLeaf(*range.low, &path, &current_leaf_page_id, &existing_rid);
        if (!status.ok()) {
            return status;
        }
    }

    while (!IsInvalidPageId(current_leaf_page_id)) {
        Page leaf_page;
        status = ReadPage(current_leaf_page_id, &leaf_page);
        if (!status.ok()) {
            return status;
        }

        NodeHeader header;
        LeafNodeData leaf;
        status = ParseLeafPage(leaf_page, &header, &leaf);
        if (!status.ok()) {
            return status;
        }

        for (const LeafEntry& entry : leaf.entries) {
            if (range.high.has_value()) {
                if (range.include_high) {
                    if (entry.key > *range.high) {
                        return Status::Ok();
                    }
                } else if (!(entry.key < *range.high)) {
                    return Status::Ok();
                }
            }

            if (FitsInRange(entry.key, range)) {
                out->push_back(entry.rid);
            }
        }

        current_leaf_page_id = header.next_leaf_page_id;
    }

    return Status::Ok();
}

Status BStarTree::ValidateKeyType(const Value& key) const {
    if (key.is_null()) {
        return Status::Error(StatusCode::kInvalidArgument, "NULL keys are not supported in BStarTree");
    }
    if (key.type() != key_type_) {
        return Status::Error(StatusCode::kInvalidArgument, "Key type does not match index key type");
    }
    ByteBuffer encoded_key;
    Status status = KeyEncoder::Encode(key, &encoded_key);
    if (!status.ok()) {
        return status;
    }
    if (encoded_key.size() > max_key_payload_bytes_) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Encoded index key exceeds configured maximum payload size");
    }
    return Status::Ok();
}

Status BStarTree::ValidateRange(const KeyRange& range) const {
    if (range.low.has_value()) {
        Status status = ValidateKeyType(*range.low);
        if (!status.ok()) {
            return status;
        }
    }
    if (range.high.has_value()) {
        Status status = ValidateKeyType(*range.high);
        if (!status.ok()) {
            return status;
        }
    }
    if (range.low.has_value() && range.high.has_value() && *range.high < *range.low) {
        return Status::Error(StatusCode::kInvalidArgument, "Range high bound is smaller than low bound");
    }
    return Status::Ok();
}

Status BStarTree::InitializeTree() {
    PageId metadata_page_id;
    Status status = page_manager_.AllocatePage(&metadata_page_id);
    if (!status.ok()) {
        return status;
    }
    if (metadata_page_id != PageId{0}) {
        return Status::Error(StatusCode::kInternalError, "Metadata page must be page 0");
    }

    metadata_.version = kTreeVersion;
    metadata_.root_page_id = InvalidPageId();
    metadata_.first_leaf_page_id = InvalidPageId();
    metadata_.min_degree = min_degree_;
    metadata_.max_key_payload_bytes = max_key_payload_bytes_;
    metadata_.tree_height = 0;
    return WriteMetadata();
}

Status BStarTree::LoadMetadata() {
    Page metadata_page;
    Status status = ReadPage(PageId{0}, &metadata_page);
    if (!status.ok()) {
        return status;
    }
    return DeserializeMetadata(metadata_page.data, &metadata_);
}

Status BStarTree::WriteMetadata() {
    ByteBuffer bytes;
    Status status = SerializeMetadata(&bytes);
    if (!status.ok()) {
        return status;
    }
    if (bytes.size() > page_size_) {
        return Status::Error(StatusCode::kInternalError, "Metadata page does not fit into page size");
    }

    Page metadata_page;
    metadata_page.id = PageId{0};
    metadata_page.data.assign(page_size_, 0);
    std::copy(bytes.begin(), bytes.end(), metadata_page.data.begin());
    return WritePage(metadata_page);
}

Status BStarTree::ReadPage(PageId page_id, Page* out) const {
    return page_manager_.ReadPage(page_id, out);
}

Status BStarTree::WritePage(const Page& page) {
    Status status = page_manager_.WritePage(page);
    if (!status.ok()) {
        return status;
    }
    return page_manager_.Flush();
}

Status BStarTree::AllocateNodePage(PageId* out_page_id) {
    Status status = page_manager_.AllocatePage(out_page_id);
    if (!status.ok()) {
        return status;
    }
    return page_manager_.Flush();
}

Status BStarTree::SerializeMetadata(ByteBuffer* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }
    out->clear();
    out->insert(out->end(), kTreeMetadataMagic.begin(), kTreeMetadataMagic.end());
    binary_io::WriteUint32(out, metadata_.version);
    binary_io::WriteUint32(out, static_cast<std::uint32_t>(key_type_));
    binary_io::WriteUint64(out, metadata_.root_page_id.value);
    binary_io::WriteUint64(out, metadata_.first_leaf_page_id.value);
    binary_io::WriteUint64(out, metadata_.min_degree);
    binary_io::WriteUint64(out, metadata_.max_key_payload_bytes);
    binary_io::WriteUint64(out, metadata_.tree_height);
    return Status::Ok();
}

Status BStarTree::DeserializeMetadata(const ByteBuffer& bytes, TreeMetadata* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output metadata must not be nullptr");
    }
    if (bytes.size() < MetadataHeaderSize()) {
        return Status::Error(StatusCode::kInvalidArgument, "Metadata page is too small");
    }
    if (!std::equal(kTreeMetadataMagic.begin(), kTreeMetadataMagic.end(), bytes.begin())) {
        return Status::Error(StatusCode::kInvalidArgument, "Invalid BStarTree metadata magic");
    }

    std::size_t offset = kTreeMetadataMagic.size();
    std::uint32_t version = 0;
    std::uint32_t raw_key_type = 0;
    Status status = binary_io::ReadUint32(bytes, &offset, &version);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint32(bytes, &offset, &raw_key_type);
    if (!status.ok()) {
        return status;
    }
    if (version != kTreeVersion) {
        return Status::Error(StatusCode::kInvalidArgument, "Unsupported BStarTree version");
    }
    if (static_cast<ValueType>(raw_key_type) != key_type_) {
        return Status::Error(StatusCode::kInvalidArgument, "Index key type does not match tree metadata");
    }

    out->version = version;
    status = binary_io::ReadUint64(bytes, &offset, &out->root_page_id.value);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint64(bytes, &offset, &out->first_leaf_page_id.value);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint64(bytes, &offset, &out->min_degree);
    if (!status.ok()) {
        return status;
    }
    if (out->min_degree != min_degree_) {
        return Status::Error(StatusCode::kInvalidArgument, "Index min_degree does not match tree metadata");
    }
    status = binary_io::ReadUint64(bytes, &offset, &out->max_key_payload_bytes);
    if (!status.ok()) {
        return status;
    }
    if (out->max_key_payload_bytes != max_key_payload_bytes_) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Index key payload policy does not match tree metadata");
    }
    return binary_io::ReadUint64(bytes, &offset, &out->tree_height);
}

Status BStarTree::SerializeNodeHeader(const NodeHeader& header, ByteBuffer* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }
    out->clear();
    out->insert(out->end(), kTreeNodeMagic.begin(), kTreeNodeMagic.end());
    binary_io::WriteUint32(out, static_cast<std::uint32_t>(header.type));
    binary_io::WriteUint32(out, header.key_count);
    binary_io::WriteUint64(out, header.parent_page_id.value);
    binary_io::WriteUint64(out, header.next_leaf_page_id.value);
    binary_io::WriteUint8(out, header.is_root ? 1U : 0U);
    return Status::Ok();
}

Status BStarTree::DeserializeNodeHeader(const ByteBuffer& bytes, NodeHeader* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output header must not be nullptr");
    }
    if (bytes.size() < NodeHeaderSize()) {
        return Status::Error(StatusCode::kInvalidArgument, "Node page is too small");
    }
    if (!std::equal(kTreeNodeMagic.begin(), kTreeNodeMagic.end(), bytes.begin())) {
        return Status::Error(StatusCode::kInvalidArgument, "Invalid BStarTree node magic");
    }

    std::size_t offset = kTreeNodeMagic.size();
    std::uint32_t raw_type = 0;
    std::uint8_t is_root = 0;
    Status status = binary_io::ReadUint32(bytes, &offset, &raw_type);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint32(bytes, &offset, &out->key_count);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint64(bytes, &offset, &out->parent_page_id.value);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint64(bytes, &offset, &out->next_leaf_page_id.value);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::ReadUint8(bytes, &offset, &is_root);
    if (!status.ok()) {
        return status;
    }
    if (raw_type != static_cast<std::uint32_t>(NodeType::kLeaf) &&
        raw_type != static_cast<std::uint32_t>(NodeType::kInternal)) {
        return Status::Error(StatusCode::kInvalidArgument, "Invalid BStarTree node type");
    }
    out->type = static_cast<NodeType>(raw_type);
    out->is_root = is_root != 0U;
    return Status::Ok();
}

Status BStarTree::ParseLeafPage(const Page& page, NodeHeader* out_header, LeafNodeData* out_leaf) const {
    if (out_header == nullptr || out_leaf == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "leaf parse outputs must not be nullptr");
    }

    Status status = DeserializeNodeHeader(page.data, out_header);
    if (!status.ok()) {
        return status;
    }
    if (out_header->type != NodeType::kLeaf) {
        return Status::Error(StatusCode::kInvalidArgument, "Expected leaf page");
    }

    out_leaf->entries.clear();
    out_leaf->entries.reserve(out_header->key_count);
    std::size_t offset = NodeHeaderSize();
    for (std::uint32_t i = 0; i < out_header->key_count; ++i) {
        LeafEntry entry;
        status = DeserializeLeafEntry(page.data, &offset, &entry);
        if (!status.ok()) {
            return status;
        }
        out_leaf->entries.push_back(std::move(entry));
    }
    return Status::Ok();
}

Status BStarTree::BuildLeafPage(PageId page_id,
                                const NodeHeader& header,
                                const LeafNodeData& leaf,
                                Page* out_page) const {
    if (out_page == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output page must not be nullptr");
    }

    NodeHeader actual_header = header;
    actual_header.type = NodeType::kLeaf;
    actual_header.key_count = static_cast<std::uint32_t>(leaf.entries.size());

    ByteBuffer bytes;
    Status status = SerializeNodeHeader(actual_header, &bytes);
    if (!status.ok()) {
        return status;
    }

    for (const LeafEntry& entry : leaf.entries) {
        ByteBuffer entry_bytes;
        status = SerializeLeafEntry(entry, &entry_bytes);
        if (!status.ok()) {
            return status;
        }
        bytes.insert(bytes.end(), entry_bytes.begin(), entry_bytes.end());
    }

    if (bytes.size() > page_size_) {
        return Status::Error(StatusCode::kInvalidArgument, "Leaf page does not fit into page size");
    }

    out_page->id = page_id;
    out_page->data.assign(page_size_, 0);
    std::copy(bytes.begin(), bytes.end(), out_page->data.begin());
    return Status::Ok();
}

Status BStarTree::ParseInternalPage(const Page& page,
                                    NodeHeader* out_header,
                                    InternalNodeData* out_internal) const {
    if (out_header == nullptr || out_internal == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "internal parse outputs must not be nullptr");
    }

    Status status = DeserializeNodeHeader(page.data, out_header);
    if (!status.ok()) {
        return status;
    }
    if (out_header->type != NodeType::kInternal) {
        return Status::Error(StatusCode::kInvalidArgument, "Expected internal page");
    }

    out_internal->separator_keys.clear();
    out_internal->child_page_ids.clear();
    out_internal->separator_keys.reserve(out_header->key_count);
    out_internal->child_page_ids.reserve(static_cast<std::size_t>(out_header->key_count) + 1U);

    std::size_t offset = NodeHeaderSize();
    PageId child_page_id;
    status = binary_io::ReadUint64(page.data, &offset, &child_page_id.value);
    if (!status.ok()) {
        return status;
    }
    out_internal->child_page_ids.push_back(child_page_id);

    for (std::uint32_t i = 0; i < out_header->key_count; ++i) {
        Value separator;
        status = DeserializeSeparatorKey(page.data, &offset, &separator);
        if (!status.ok()) {
            return status;
        }
        out_internal->separator_keys.push_back(std::move(separator));

        status = binary_io::ReadUint64(page.data, &offset, &child_page_id.value);
        if (!status.ok()) {
            return status;
        }
        out_internal->child_page_ids.push_back(child_page_id);
    }

    return Status::Ok();
}

Status BStarTree::BuildInternalPage(PageId page_id,
                                    const NodeHeader& header,
                                    const InternalNodeData& internal,
                                    Page* out_page) const {
    if (out_page == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output page must not be nullptr");
    }
    if (internal.child_page_ids.size() != internal.separator_keys.size() + 1U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Internal node must contain exactly key_count + 1 child pointers");
    }

    NodeHeader actual_header = header;
    actual_header.type = NodeType::kInternal;
    actual_header.key_count = static_cast<std::uint32_t>(internal.separator_keys.size());
    actual_header.next_leaf_page_id = InvalidPageId();

    ByteBuffer bytes;
    Status status = SerializeNodeHeader(actual_header, &bytes);
    if (!status.ok()) {
        return status;
    }

    binary_io::WriteUint64(&bytes, internal.child_page_ids[0].value);
    for (std::size_t i = 0; i < internal.separator_keys.size(); ++i) {
        ByteBuffer key_bytes;
        status = SerializeSeparatorKey(internal.separator_keys[i], &key_bytes);
        if (!status.ok()) {
            return status;
        }
        bytes.insert(bytes.end(), key_bytes.begin(), key_bytes.end());
        binary_io::WriteUint64(&bytes, internal.child_page_ids[i + 1U].value);
    }

    if (bytes.size() > page_size_) {
        return Status::Error(StatusCode::kInvalidArgument, "Internal page does not fit into page size");
    }

    out_page->id = page_id;
    out_page->data.assign(page_size_, 0);
    std::copy(bytes.begin(), bytes.end(), out_page->data.begin());
    return Status::Ok();
}

Status BStarTree::SerializeLeafEntry(const LeafEntry& entry, ByteBuffer* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }
    out->clear();

    ByteBuffer key_bytes;
    Status status = KeyEncoder::Encode(entry.key, &key_bytes);
    if (!status.ok()) {
        return status;
    }

    binary_io::WriteUint32(out, static_cast<std::uint32_t>(key_bytes.size()));
    out->insert(out->end(), key_bytes.begin(), key_bytes.end());
    binary_io::WriteUint64(out, entry.rid.value);
    return Status::Ok();
}

Status BStarTree::DeserializeLeafEntry(const ByteBuffer& bytes, std::size_t* offset, LeafEntry* out) const {
    if (offset == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "offset and out must not be nullptr");
    }

    std::uint32_t key_size = 0;
    Status status = binary_io::ReadUint32(bytes, offset, &key_size);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::EnsureReadable(bytes, *offset, key_size);
    if (!status.ok()) {
        return status;
    }

    ByteBuffer key_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(*offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(*offset + key_size));
    *offset += key_size;

    status = KeyEncoder::Decode(key_bytes, key_type_, &out->key);
    if (!status.ok()) {
        return status;
    }
    return binary_io::ReadUint64(bytes, offset, &out->rid.value);
}

Status BStarTree::SerializeSeparatorKey(const Value& key, ByteBuffer* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }
    out->clear();

    ByteBuffer key_bytes;
    Status status = KeyEncoder::Encode(key, &key_bytes);
    if (!status.ok()) {
        return status;
    }

    binary_io::WriteUint32(out, static_cast<std::uint32_t>(key_bytes.size()));
    out->insert(out->end(), key_bytes.begin(), key_bytes.end());
    return Status::Ok();
}

Status BStarTree::DeserializeSeparatorKey(const ByteBuffer& bytes, std::size_t* offset, Value* out) const {
    if (offset == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "offset and out must not be nullptr");
    }

    std::uint32_t key_size = 0;
    Status status = binary_io::ReadUint32(bytes, offset, &key_size);
    if (!status.ok()) {
        return status;
    }
    status = binary_io::EnsureReadable(bytes, *offset, key_size);
    if (!status.ok()) {
        return status;
    }

    ByteBuffer key_bytes(bytes.begin() + static_cast<std::ptrdiff_t>(*offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(*offset + key_size));
    *offset += key_size;
    return KeyEncoder::Decode(key_bytes, key_type_, out);
}

Status BStarTree::FindPathToLeaf(const Value& key,
                                 std::vector<SearchPathEntry>* out_path,
                                 PageId* out_leaf_page_id,
                                 std::optional<RowId>* out_existing_rid) const {
    if (out_path == nullptr || out_leaf_page_id == nullptr || out_existing_rid == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "FindPathToLeaf outputs must not be nullptr");
    }

    out_path->clear();
    *out_leaf_page_id = InvalidPageId();
    *out_existing_rid = std::nullopt;

    if (IsInvalidPageId(metadata_.root_page_id)) {
        return Status::Ok();
    }

    PageId current_page_id = metadata_.root_page_id;
    while (true) {
        Page page;
        Status status = ReadPage(current_page_id, &page);
        if (!status.ok()) {
            return status;
        }

        NodeHeader header;
        status = DeserializeNodeHeader(page.data, &header);
        if (!status.ok()) {
            return status;
        }

        if (header.type == NodeType::kLeaf) {
            LeafNodeData leaf;
            status = ParseLeafPage(page, &header, &leaf);
            if (!status.ok()) {
                return status;
            }

            const std::size_t pos = FindEntryPosition(leaf.entries, key);
            if (pos < leaf.entries.size() && leaf.entries[pos].key == key) {
                *out_existing_rid = leaf.entries[pos].rid;
            }
            *out_leaf_page_id = current_page_id;
            return Status::Ok();
        }

        InternalNodeData internal;
        status = ParseInternalPage(page, &header, &internal);
        if (!status.ok()) {
            return status;
        }

        const std::size_t child_index = ChooseChildIndex(internal, key);
        out_path->push_back(SearchPathEntry{current_page_id, child_index});
        current_page_id = internal.child_page_ids[child_index];
    }
}

Status BStarTree::FindMinimumKey(PageId subtree_root, Value* out_key) const {
    if (out_key == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_key must not be nullptr");
    }
    if (IsInvalidPageId(subtree_root)) {
        return Status::Error(StatusCode::kInvalidArgument, "subtree root is invalid");
    }

    PageId current_page_id = subtree_root;
    while (true) {
        Page page;
        Status status = ReadPage(current_page_id, &page);
        if (!status.ok()) {
            return status;
        }

        NodeHeader header;
        status = DeserializeNodeHeader(page.data, &header);
        if (!status.ok()) {
            return status;
        }

        if (header.type == NodeType::kLeaf) {
            LeafNodeData leaf;
            status = ParseLeafPage(page, &header, &leaf);
            if (!status.ok()) {
                return status;
            }
            if (leaf.entries.empty()) {
                return Status::Error(StatusCode::kInternalError, "Leaf node is unexpectedly empty");
            }
            *out_key = leaf.entries.front().key;
            return Status::Ok();
        }

        InternalNodeData internal;
        status = ParseInternalPage(page, &header, &internal);
        if (!status.ok()) {
            return status;
        }
        current_page_id = internal.child_page_ids.front();
    }
}

bool BStarTree::IsLeafUnderfull(const NodeHeader& header, const LeafNodeData& leaf) const {
    if (header.is_root) {
        return false;
    }
    return leaf.entries.size() < MinKeysPerNonRootNode();
}

bool BStarTree::IsInternalUnderfull(const NodeHeader& header, const InternalNodeData& internal) const {
    if (header.is_root) {
        return false;
    }
    return internal.separator_keys.size() < MinKeysPerNonRootNode();
}

std::size_t BStarTree::MaxKeysPerNode() const {
    return 2U * min_degree_ - 1U;
}

std::size_t BStarTree::MinKeysPerNonRootNode() const {
    return min_degree_ - 1U;
}

std::size_t BStarTree::FindEntryPosition(const std::vector<LeafEntry>& entries, const Value& key) const {
    return static_cast<std::size_t>(std::lower_bound(entries.begin(),
                                                     entries.end(),
                                                     key,
                                                     [](const LeafEntry& entry, const Value& value) {
                                                         return entry.key < value;
                                                     }) -
                                    entries.begin());
}

std::size_t BStarTree::ChooseChildIndex(const InternalNodeData& internal, const Value& key) const {
    for (std::size_t i = 0; i < internal.separator_keys.size(); ++i) {
        if (key < internal.separator_keys[i]) {
            return i;
        }
    }
    return internal.separator_keys.size();
}

Status BStarTree::InsertIntoLeaf(PageId leaf_page_id,
                                 const std::vector<SearchPathEntry>& path,
                                 const Value& key,
                                 RowId rid) {
    Page page;
    Status status = ReadPage(leaf_page_id, &page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader header;
    LeafNodeData leaf;
    status = ParseLeafPage(page, &header, &leaf);
    if (!status.ok()) {
        return status;
    }

    const std::size_t pos = FindEntryPosition(leaf.entries, key);
    leaf.entries.insert(leaf.entries.begin() + static_cast<std::ptrdiff_t>(pos), LeafEntry{key, rid});

    if (leaf.entries.size() <= MaxKeysPerNode()) {
        Page rebuilt;
        status = BuildLeafPage(leaf_page_id, header, leaf, &rebuilt);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(rebuilt);
        if (!status.ok()) {
            return status;
        }
        if (pos == 0U) {
            return UpdateParentSeparatorAfterChildChange(path, leaf.entries.front().key);
        }
        return Status::Ok();
    }

    bool redistributed = false;
    status = RedistributeLeafWithRightSibling(leaf_page_id, path, leaf, &redistributed);
    if (!status.ok()) {
        return status;
    }
    if (redistributed) {
        return Status::Ok();
    }

    return SplitLeaf(leaf_page_id, path, leaf);
}

Status BStarTree::InsertIntoInternal(PageId internal_page_id,
                                     const std::vector<SearchPathEntry>& path,
                                     std::size_t left_child_index,
                                     const Value& separator_key,
                                     PageId right_child_page_id) {
    Page page;
    Status status = ReadPage(internal_page_id, &page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader header;
    InternalNodeData internal;
    status = ParseInternalPage(page, &header, &internal);
    if (!status.ok()) {
        return status;
    }

    internal.separator_keys.insert(internal.separator_keys.begin() + static_cast<std::ptrdiff_t>(left_child_index),
                                   separator_key);
    internal.child_page_ids.insert(
        internal.child_page_ids.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1U), right_child_page_id);

    if (internal.separator_keys.size() <= MaxKeysPerNode()) {
        Page rebuilt;
        status = BuildInternalPage(internal_page_id, header, internal, &rebuilt);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(rebuilt);
        if (!status.ok()) {
            return status;
        }

        Page right_page;
        status = ReadPage(right_child_page_id, &right_page);
        if (!status.ok()) {
            return status;
        }

        NodeHeader right_header;
        status = DeserializeNodeHeader(right_page.data, &right_header);
        if (!status.ok()) {
            return status;
        }
        right_header.parent_page_id = internal_page_id;

        if (right_header.type == NodeType::kLeaf) {
            LeafNodeData right_leaf;
            status = ParseLeafPage(right_page, &right_header, &right_leaf);
            if (!status.ok()) {
                return status;
            }
            Page updated_right_page;
            status = BuildLeafPage(right_child_page_id, right_header, right_leaf, &updated_right_page);
            if (!status.ok()) {
                return status;
            }
            return WritePage(updated_right_page);
        }

        InternalNodeData right_internal;
        status = ParseInternalPage(right_page, &right_header, &right_internal);
        if (!status.ok()) {
            return status;
        }
        Page updated_right_page;
        status = BuildInternalPage(right_child_page_id, right_header, right_internal, &updated_right_page);
        if (!status.ok()) {
            return status;
        }
        return WritePage(updated_right_page);
    }

    bool redistributed = false;
    status = RedistributeInternalWithRightSibling(internal_page_id, path, internal, &redistributed);
    if (!status.ok()) {
        return status;
    }
    if (redistributed) {
        return Status::Ok();
    }

    return SplitInternal(internal_page_id, path, internal);
}

Status BStarTree::SplitLeaf(PageId leaf_page_id,
                            const std::vector<SearchPathEntry>& path,
                            const LeafNodeData& expanded_leaf) {
    if (expanded_leaf.entries.size() != MaxKeysPerNode() + 1U) {
        return Status::Error(StatusCode::kInternalError, "Leaf split requires exactly max_keys + 1 entries");
    }

    Page current_page;
    Status status = ReadPage(leaf_page_id, &current_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader current_header;
    LeafNodeData ignored_leaf;
    status = ParseLeafPage(current_page, &current_header, &ignored_leaf);
    if (!status.ok()) {
        return status;
    }

    const std::size_t split_index = min_degree_;
    LeafNodeData left_leaf;
    LeafNodeData right_leaf;
    left_leaf.entries.assign(expanded_leaf.entries.begin(),
                             expanded_leaf.entries.begin() + static_cast<std::ptrdiff_t>(split_index));
    right_leaf.entries.assign(expanded_leaf.entries.begin() + static_cast<std::ptrdiff_t>(split_index),
                              expanded_leaf.entries.end());

    if (left_leaf.entries.empty() || right_leaf.entries.empty()) {
        return Status::Error(StatusCode::kInternalError, "Split produced an empty leaf");
    }

    PageId right_page_id;
    status = AllocateNodePage(&right_page_id);
    if (!status.ok()) {
        return status;
    }

    NodeHeader left_header = current_header;
    NodeHeader right_header = current_header;
    left_header.is_root = false;
    right_header.is_root = false;
    left_header.parent_page_id = path.empty() ? InvalidPageId() : path.back().page_id;
    right_header.parent_page_id = left_header.parent_page_id;
    right_header.next_leaf_page_id = current_header.next_leaf_page_id;
    left_header.next_leaf_page_id = right_page_id;

    if (path.empty()) {
        PageId root_page_id;
        status = AllocateNodePage(&root_page_id);
        if (!status.ok()) {
            return status;
        }
        left_header.parent_page_id = root_page_id;
        right_header.parent_page_id = root_page_id;

        Page left_page;
        status = BuildLeafPage(leaf_page_id, left_header, left_leaf, &left_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(left_page);
        if (!status.ok()) {
            return status;
        }

        Page right_page;
        status = BuildLeafPage(right_page_id, right_header, right_leaf, &right_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(right_page);
        if (!status.ok()) {
            return status;
        }

        NodeHeader root_header;
        root_header.type = NodeType::kInternal;
        root_header.parent_page_id = InvalidPageId();
        root_header.next_leaf_page_id = InvalidPageId();
        root_header.is_root = true;

        InternalNodeData root_data;
        root_data.separator_keys = {right_leaf.entries.front().key};
        root_data.child_page_ids = {leaf_page_id, right_page_id};

        Page root_page;
        status = BuildInternalPage(root_page_id, root_header, root_data, &root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(root_page);
        if (!status.ok()) {
            return status;
        }

        metadata_.root_page_id = root_page_id;
        metadata_.first_leaf_page_id = leaf_page_id;
        metadata_.tree_height += 1U;
        return WriteMetadata();
    }

    Page left_page;
    status = BuildLeafPage(leaf_page_id, left_header, left_leaf, &left_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(left_page);
    if (!status.ok()) {
        return status;
    }

    Page right_page;
    status = BuildLeafPage(right_page_id, right_header, right_leaf, &right_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(right_page);
    if (!status.ok()) {
        return status;
    }

    std::vector<SearchPathEntry> parent_path = path;
    parent_path.pop_back();
    return InsertIntoInternal(path.back().page_id,
                              parent_path,
                              path.back().child_index,
                              right_leaf.entries.front().key,
                              right_page_id);
}

Status BStarTree::SplitInternal(PageId internal_page_id,
                                const std::vector<SearchPathEntry>& path,
                                const InternalNodeData& expanded_internal) {
    if (expanded_internal.separator_keys.size() != MaxKeysPerNode() + 1U) {
        return Status::Error(StatusCode::kInternalError,
                             "Internal split requires exactly max_keys + 1 separator keys");
    }

    Page current_page;
    Status status = ReadPage(internal_page_id, &current_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader current_header;
    InternalNodeData ignored_internal;
    status = ParseInternalPage(current_page, &current_header, &ignored_internal);
    if (!status.ok()) {
        return status;
    }

    const std::size_t middle = min_degree_ - 1U;
    const Value promoted_separator = expanded_internal.separator_keys[middle];

    InternalNodeData left_internal;
    InternalNodeData right_internal;
    left_internal.separator_keys.assign(expanded_internal.separator_keys.begin(),
                                        expanded_internal.separator_keys.begin() +
                                            static_cast<std::ptrdiff_t>(middle));
    left_internal.child_page_ids.assign(expanded_internal.child_page_ids.begin(),
                                        expanded_internal.child_page_ids.begin() +
                                            static_cast<std::ptrdiff_t>(middle + 1U));

    right_internal.separator_keys.assign(expanded_internal.separator_keys.begin() +
                                             static_cast<std::ptrdiff_t>(middle + 1U),
                                         expanded_internal.separator_keys.end());
    right_internal.child_page_ids.assign(expanded_internal.child_page_ids.begin() +
                                             static_cast<std::ptrdiff_t>(middle + 1U),
                                         expanded_internal.child_page_ids.end());

    if (left_internal.child_page_ids.empty() || right_internal.child_page_ids.empty()) {
        return Status::Error(StatusCode::kInternalError, "Split produced an invalid internal node");
    }

    PageId right_page_id;
    status = AllocateNodePage(&right_page_id);
    if (!status.ok()) {
        return status;
    }

    auto rewrite_child_parent = [&](const InternalNodeData& node_data, PageId parent_page_id) -> Status {
        for (PageId child_page_id : node_data.child_page_ids) {
            Page child_page;
            Status child_status = ReadPage(child_page_id, &child_page);
            if (!child_status.ok()) {
                return child_status;
            }

            NodeHeader child_header;
            child_status = DeserializeNodeHeader(child_page.data, &child_header);
            if (!child_status.ok()) {
                return child_status;
            }
            child_header.parent_page_id = parent_page_id;

            if (child_header.type == NodeType::kLeaf) {
                LeafNodeData child_leaf;
                child_status = ParseLeafPage(child_page, &child_header, &child_leaf);
                if (!child_status.ok()) {
                    return child_status;
                }
                Page rebuilt_child_page;
                child_status = BuildLeafPage(child_page_id, child_header, child_leaf, &rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                child_status = WritePage(rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                continue;
            }

            InternalNodeData child_internal;
            child_status = ParseInternalPage(child_page, &child_header, &child_internal);
            if (!child_status.ok()) {
                return child_status;
            }
            Page rebuilt_child_page;
            child_status = BuildInternalPage(child_page_id, child_header, child_internal, &rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
            child_status = WritePage(rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
        }
        return Status::Ok();
    };

    NodeHeader left_header = current_header;
    NodeHeader right_header = current_header;
    left_header.is_root = false;
    right_header.is_root = false;
    left_header.parent_page_id = path.empty() ? InvalidPageId() : path.back().page_id;
    right_header.parent_page_id = left_header.parent_page_id;
    left_header.next_leaf_page_id = InvalidPageId();
    right_header.next_leaf_page_id = InvalidPageId();

    if (path.empty()) {
        PageId root_page_id;
        status = AllocateNodePage(&root_page_id);
        if (!status.ok()) {
            return status;
        }

        left_header.parent_page_id = root_page_id;
        right_header.parent_page_id = root_page_id;

        Page left_page;
        status = BuildInternalPage(internal_page_id, left_header, left_internal, &left_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(left_page);
        if (!status.ok()) {
            return status;
        }

        Page right_page;
        status = BuildInternalPage(right_page_id, right_header, right_internal, &right_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(right_page);
        if (!status.ok()) {
            return status;
        }

        status = rewrite_child_parent(left_internal, internal_page_id);
        if (!status.ok()) {
            return status;
        }
        status = rewrite_child_parent(right_internal, right_page_id);
        if (!status.ok()) {
            return status;
        }

        NodeHeader root_header;
        root_header.type = NodeType::kInternal;
        root_header.parent_page_id = InvalidPageId();
        root_header.next_leaf_page_id = InvalidPageId();
        root_header.is_root = true;

        InternalNodeData root_data;
        root_data.separator_keys = {promoted_separator};
        root_data.child_page_ids = {internal_page_id, right_page_id};

        Page root_page;
        status = BuildInternalPage(root_page_id, root_header, root_data, &root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(root_page);
        if (!status.ok()) {
            return status;
        }

        metadata_.root_page_id = root_page_id;
        metadata_.tree_height += 1U;
        return WriteMetadata();
    }

    Page left_page;
    status = BuildInternalPage(internal_page_id, left_header, left_internal, &left_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(left_page);
    if (!status.ok()) {
        return status;
    }

    Page right_page;
    status = BuildInternalPage(right_page_id, right_header, right_internal, &right_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(right_page);
    if (!status.ok()) {
        return status;
    }

    status = rewrite_child_parent(left_internal, internal_page_id);
    if (!status.ok()) {
        return status;
    }
    status = rewrite_child_parent(right_internal, right_page_id);
    if (!status.ok()) {
        return status;
    }

    std::vector<SearchPathEntry> parent_path = path;
    parent_path.pop_back();
    return InsertIntoInternal(path.back().page_id,
                              parent_path,
                              path.back().child_index,
                              promoted_separator,
                              right_page_id);
}

Status BStarTree::RedistributeLeafWithRightSibling(PageId leaf_page_id,
                                                   const std::vector<SearchPathEntry>& path,
                                                   const LeafNodeData& expanded_leaf,
                                                   bool* out_redistributed) {
    if (out_redistributed == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_redistributed must not be nullptr");
    }
    *out_redistributed = false;
    if (path.empty()) {
        return Status::Ok();
    }

    const SearchPathEntry& parent_link = path.back();
    Page parent_page;
    Status status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }
    if (parent_link.child_index + 1U >= parent_internal.child_page_ids.size()) {
        return Status::Ok();
    }

    const PageId right_page_id = parent_internal.child_page_ids[parent_link.child_index + 1U];
    Page current_page;
    status = ReadPage(leaf_page_id, &current_page);
    if (!status.ok()) {
        return status;
    }
    Page right_page;
    status = ReadPage(right_page_id, &right_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader current_header;
    LeafNodeData ignored_current;
    status = ParseLeafPage(current_page, &current_header, &ignored_current);
    if (!status.ok()) {
        return status;
    }

    NodeHeader right_header;
    LeafNodeData right_leaf;
    status = ParseLeafPage(right_page, &right_header, &right_leaf);
    if (!status.ok()) {
        return status;
    }

    std::vector<LeafEntry> combined = expanded_leaf.entries;
    combined.insert(combined.end(), right_leaf.entries.begin(), right_leaf.entries.end());

    const std::size_t min_keys = MinKeysPerNonRootNode();
    const std::size_t max_keys = MaxKeysPerNode();
    auto split_index = ChooseBalancedSplit(combined.size(), [&](std::size_t left_count, std::size_t right_count) {
        if (left_count < min_keys || left_count > max_keys) {
            return false;
        }
        if (right_count < min_keys || right_count > max_keys) {
            return false;
        }

        LeafNodeData left_candidate;
        LeafNodeData right_candidate;
        left_candidate.entries.assign(combined.begin(), combined.begin() + static_cast<std::ptrdiff_t>(left_count));
        right_candidate.entries.assign(combined.begin() + static_cast<std::ptrdiff_t>(left_count), combined.end());

        Page test_page;
        Status left_status = BuildLeafPage(leaf_page_id, current_header, left_candidate, &test_page);
        if (!left_status.ok()) {
            return false;
        }
        Status right_status = BuildLeafPage(right_page_id, right_header, right_candidate, &test_page);
        return right_status.ok();
    });
    if (!split_index.has_value()) {
        return Status::Ok();
    }

    LeafNodeData left_leaf;
    LeafNodeData new_right_leaf;
    left_leaf.entries.assign(combined.begin(), combined.begin() + static_cast<std::ptrdiff_t>(*split_index));
    new_right_leaf.entries.assign(combined.begin() + static_cast<std::ptrdiff_t>(*split_index), combined.end());

    current_header.next_leaf_page_id = right_page_id;

    Page rebuilt_left_page;
    status = BuildLeafPage(leaf_page_id, current_header, left_leaf, &rebuilt_left_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_left_page);
    if (!status.ok()) {
        return status;
    }

    Page rebuilt_right_page;
    status = BuildLeafPage(right_page_id, right_header, new_right_leaf, &rebuilt_right_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_right_page);
    if (!status.ok()) {
        return status;
    }

    parent_internal.separator_keys[parent_link.child_index] = new_right_leaf.entries.front().key;
    Page rebuilt_parent_page;
    status = BuildInternalPage(parent_link.page_id, parent_header, parent_internal, &rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }

    if (parent_link.child_index == 0U && !left_leaf.entries.empty()) {
        status = UpdateParentSeparatorAfterChildChange(path, left_leaf.entries.front().key);
        if (!status.ok()) {
            return status;
        }
    }

    *out_redistributed = true;
    return Status::Ok();
}

Status BStarTree::RedistributeInternalWithRightSibling(PageId internal_page_id,
                                                       const std::vector<SearchPathEntry>& path,
                                                       const InternalNodeData& expanded_internal,
                                                       bool* out_redistributed) {
    if (out_redistributed == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_redistributed must not be nullptr");
    }
    *out_redistributed = false;
    if (path.empty()) {
        return Status::Ok();
    }

    const SearchPathEntry& parent_link = path.back();
    Page parent_page;
    Status status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }
    if (parent_link.child_index + 1U >= parent_internal.child_page_ids.size()) {
        return Status::Ok();
    }

    const PageId right_page_id = parent_internal.child_page_ids[parent_link.child_index + 1U];
    Page current_page;
    status = ReadPage(internal_page_id, &current_page);
    if (!status.ok()) {
        return status;
    }
    Page right_page;
    status = ReadPage(right_page_id, &right_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader current_header;
    InternalNodeData ignored_current;
    status = ParseInternalPage(current_page, &current_header, &ignored_current);
    if (!status.ok()) {
        return status;
    }

    NodeHeader right_header;
    InternalNodeData right_internal;
    status = ParseInternalPage(right_page, &right_header, &right_internal);
    if (!status.ok()) {
        return status;
    }

    std::vector<Value> all_keys = expanded_internal.separator_keys;
    all_keys.push_back(parent_internal.separator_keys[parent_link.child_index]);
    all_keys.insert(all_keys.end(), right_internal.separator_keys.begin(), right_internal.separator_keys.end());

    std::vector<PageId> all_children = expanded_internal.child_page_ids;
    all_children.insert(all_children.end(), right_internal.child_page_ids.begin(), right_internal.child_page_ids.end());

    const std::size_t min_keys = MinKeysPerNonRootNode();
    const std::size_t max_keys = MaxKeysPerNode();
    std::optional<std::size_t> best_middle;
    std::size_t best_distance = std::numeric_limits<std::size_t>::max();
    for (std::size_t middle = min_keys; middle < all_keys.size(); ++middle) {
        const std::size_t right_count = all_keys.size() - middle - 1U;
        if (middle > max_keys || right_count < min_keys || right_count > max_keys) {
            continue;
        }

        InternalNodeData left_candidate;
        InternalNodeData right_candidate;
        left_candidate.separator_keys.assign(all_keys.begin(),
                                             all_keys.begin() + static_cast<std::ptrdiff_t>(middle));
        left_candidate.child_page_ids.assign(all_children.begin(),
                                             all_children.begin() + static_cast<std::ptrdiff_t>(middle + 1U));

        right_candidate.separator_keys.assign(all_keys.begin() + static_cast<std::ptrdiff_t>(middle + 1U),
                                              all_keys.end());
        right_candidate.child_page_ids.assign(all_children.begin() + static_cast<std::ptrdiff_t>(middle + 1U),
                                              all_children.end());

        Page test_page;
        if (!BuildInternalPage(internal_page_id, current_header, left_candidate, &test_page).ok()) {
            continue;
        }
        if (!BuildInternalPage(right_page_id, right_header, right_candidate, &test_page).ok()) {
            continue;
        }

        const std::size_t distance = middle > right_count ? middle - right_count : right_count - middle;
        if (!best_middle.has_value() || distance < best_distance) {
            best_middle = middle;
            best_distance = distance;
        }
    }

    if (!best_middle.has_value()) {
        return Status::Ok();
    }

    const std::size_t middle = *best_middle;
    InternalNodeData left_internal;
    InternalNodeData new_right_internal;
    left_internal.separator_keys.assign(all_keys.begin(), all_keys.begin() + static_cast<std::ptrdiff_t>(middle));
    left_internal.child_page_ids.assign(all_children.begin(),
                                        all_children.begin() + static_cast<std::ptrdiff_t>(middle + 1U));
    new_right_internal.separator_keys.assign(all_keys.begin() + static_cast<std::ptrdiff_t>(middle + 1U),
                                             all_keys.end());
    new_right_internal.child_page_ids.assign(all_children.begin() + static_cast<std::ptrdiff_t>(middle + 1U),
                                             all_children.end());
    const Value replacement_separator = all_keys[middle];

    auto rewrite_child_parent = [&](const InternalNodeData& node_data, PageId parent_page_id) -> Status {
        for (PageId child_page_id : node_data.child_page_ids) {
            Page child_page;
            Status child_status = ReadPage(child_page_id, &child_page);
            if (!child_status.ok()) {
                return child_status;
            }

            NodeHeader child_header;
            child_status = DeserializeNodeHeader(child_page.data, &child_header);
            if (!child_status.ok()) {
                return child_status;
            }
            child_header.parent_page_id = parent_page_id;

            if (child_header.type == NodeType::kLeaf) {
                LeafNodeData child_leaf;
                child_status = ParseLeafPage(child_page, &child_header, &child_leaf);
                if (!child_status.ok()) {
                    return child_status;
                }
                Page rebuilt_child_page;
                child_status = BuildLeafPage(child_page_id, child_header, child_leaf, &rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                child_status = WritePage(rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                continue;
            }

            InternalNodeData child_internal;
            child_status = ParseInternalPage(child_page, &child_header, &child_internal);
            if (!child_status.ok()) {
                return child_status;
            }
            Page rebuilt_child_page;
            child_status = BuildInternalPage(child_page_id, child_header, child_internal, &rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
            child_status = WritePage(rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
        }
        return Status::Ok();
    };

    Page rebuilt_current_page;
    status = BuildInternalPage(internal_page_id, current_header, left_internal, &rebuilt_current_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_current_page);
    if (!status.ok()) {
        return status;
    }

    Page rebuilt_right_page;
    status = BuildInternalPage(right_page_id, right_header, new_right_internal, &rebuilt_right_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_right_page);
    if (!status.ok()) {
        return status;
    }

    parent_internal.separator_keys[parent_link.child_index] = replacement_separator;
    Page rebuilt_parent_page;
    status = BuildInternalPage(parent_link.page_id, parent_header, parent_internal, &rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }

    status = rewrite_child_parent(left_internal, internal_page_id);
    if (!status.ok()) {
        return status;
    }
    status = rewrite_child_parent(new_right_internal, right_page_id);
    if (!status.ok()) {
        return status;
    }

    *out_redistributed = true;
    return Status::Ok();
}

Status BStarTree::DeleteFromLeaf(PageId leaf_page_id,
                                 const std::vector<SearchPathEntry>& path,
                                 const Value& key) {
    Page page;
    Status status = ReadPage(leaf_page_id, &page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader header;
    LeafNodeData leaf;
    status = ParseLeafPage(page, &header, &leaf);
    if (!status.ok()) {
        return status;
    }

    const std::size_t pos = FindEntryPosition(leaf.entries, key);
    if (pos >= leaf.entries.size() || !(leaf.entries[pos].key == key)) {
        return Status::Error(StatusCode::kNotFound, "Index key not found");
    }

    const bool removed_first = pos == 0U;
    leaf.entries.erase(leaf.entries.begin() + static_cast<std::ptrdiff_t>(pos));

    if (header.is_root) {
        if (leaf.entries.empty()) {
            metadata_.root_page_id = InvalidPageId();
            metadata_.first_leaf_page_id = InvalidPageId();
            metadata_.tree_height = 0;
            return WriteMetadata();
        }

        Page rebuilt_root_page;
        status = BuildLeafPage(leaf_page_id, header, leaf, &rebuilt_root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(rebuilt_root_page);
        if (!status.ok()) {
            return status;
        }
        if (removed_first) {
            return UpdateParentSeparatorAfterChildChange(path, leaf.entries.front().key);
        }
        return Status::Ok();
    }

    Page rebuilt_leaf_page;
    status = BuildLeafPage(leaf_page_id, header, leaf, &rebuilt_leaf_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_leaf_page);
    if (!status.ok()) {
        return status;
    }

    if (!leaf.entries.empty() && removed_first) {
        status = UpdateParentSeparatorAfterChildChange(path, leaf.entries.front().key);
        if (!status.ok()) {
            return status;
        }
    }

    if (IsLeafUnderfull(header, leaf)) {
        return RebalanceAfterDelete(leaf_page_id, path);
    }
    return Status::Ok();
}

Status BStarTree::RebalanceAfterDelete(PageId page_id, const std::vector<SearchPathEntry>& path) {
    if (path.empty()) {
        return Status::Ok();
    }

    Page current_page;
    Status status = ReadPage(page_id, &current_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader current_header;
    status = DeserializeNodeHeader(current_page.data, &current_header);
    if (!status.ok()) {
        return status;
    }

    if (current_header.type == NodeType::kLeaf) {
        LeafNodeData current_leaf;
        status = ParseLeafPage(current_page, &current_header, &current_leaf);
        if (!status.ok()) {
            return status;
        }
        if (!IsLeafUnderfull(current_header, current_leaf)) {
            return Status::Ok();
        }
    } else {
        InternalNodeData current_internal;
        status = ParseInternalPage(current_page, &current_header, &current_internal);
        if (!status.ok()) {
            return status;
        }
        if (!IsInternalUnderfull(current_header, current_internal)) {
            return Status::Ok();
        }
    }

    const SearchPathEntry& parent_link = path.back();
    Page parent_page;
    status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }

    if (parent_link.child_index + 1U < parent_internal.child_page_ids.size()) {
        bool redistributed = false;
        if (current_header.type == NodeType::kLeaf) {
            LeafNodeData current_leaf;
            status = ParseLeafPage(current_page, &current_header, &current_leaf);
            if (!status.ok()) {
                return status;
            }
            status = RedistributeLeafWithRightSibling(page_id, path, current_leaf, &redistributed);
        } else {
            InternalNodeData current_internal;
            status = ParseInternalPage(current_page, &current_header, &current_internal);
            if (!status.ok()) {
                return status;
            }
            status = RedistributeInternalWithRightSibling(page_id, path, current_internal, &redistributed);
        }
        if (!status.ok()) {
            return status;
        }
        if (redistributed) {
            return Status::Ok();
        }

        bool merged = false;
        if (current_header.type == NodeType::kLeaf) {
            status = MergeLeafWithRightSibling(page_id, path, &merged);
        } else {
            status = MergeInternalWithRightSibling(page_id, path, &merged);
        }
        if (!status.ok()) {
            return status;
        }
        if (merged) {
            return Status::Ok();
        }
    }

    if (parent_link.child_index == 0U) {
        return Status::Ok();
    }

    std::vector<SearchPathEntry> left_path = path;
    left_path.back().child_index -= 1U;
    const PageId left_sibling_page_id = parent_internal.child_page_ids[parent_link.child_index - 1U];
    Page left_page;
    status = ReadPage(left_sibling_page_id, &left_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader left_header;
    status = DeserializeNodeHeader(left_page.data, &left_header);
    if (!status.ok()) {
        return status;
    }

    bool redistributed = false;
    if (left_header.type == NodeType::kLeaf) {
        LeafNodeData left_leaf;
        status = ParseLeafPage(left_page, &left_header, &left_leaf);
        if (!status.ok()) {
            return status;
        }
        status = RedistributeLeafWithRightSibling(left_sibling_page_id, left_path, left_leaf, &redistributed);
    } else {
        InternalNodeData left_internal;
        status = ParseInternalPage(left_page, &left_header, &left_internal);
        if (!status.ok()) {
            return status;
        }
        status = RedistributeInternalWithRightSibling(left_sibling_page_id, left_path, left_internal, &redistributed);
    }
    if (!status.ok()) {
        return status;
    }
    if (redistributed) {
        return Status::Ok();
    }

    bool merged = false;
    if (left_header.type == NodeType::kLeaf) {
        status = MergeLeafWithRightSibling(left_sibling_page_id, left_path, &merged);
    } else {
        status = MergeInternalWithRightSibling(left_sibling_page_id, left_path, &merged);
    }
    if (!status.ok()) {
        return status;
    }
    return Status::Ok();
}

Status BStarTree::MergeLeafWithRightSibling(PageId leaf_page_id,
                                            const std::vector<SearchPathEntry>& path,
                                            bool* out_merged) {
    if (out_merged == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_merged must not be nullptr");
    }
    *out_merged = false;
    if (path.empty()) {
        return Status::Ok();
    }

    const SearchPathEntry& parent_link = path.back();
    Page parent_page;
    Status status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }
    if (parent_link.child_index + 1U >= parent_internal.child_page_ids.size()) {
        return Status::Ok();
    }

    const PageId right_page_id = parent_internal.child_page_ids[parent_link.child_index + 1U];
    Page left_page;
    status = ReadPage(leaf_page_id, &left_page);
    if (!status.ok()) {
        return status;
    }
    Page right_page;
    status = ReadPage(right_page_id, &right_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader left_header;
    LeafNodeData left_leaf;
    status = ParseLeafPage(left_page, &left_header, &left_leaf);
    if (!status.ok()) {
        return status;
    }
    NodeHeader right_header;
    LeafNodeData right_leaf;
    status = ParseLeafPage(right_page, &right_header, &right_leaf);
    if (!status.ok()) {
        return status;
    }

    left_leaf.entries.insert(left_leaf.entries.end(), right_leaf.entries.begin(), right_leaf.entries.end());
    left_header.next_leaf_page_id = right_header.next_leaf_page_id;

    Page rebuilt_left_page;
    status = BuildLeafPage(leaf_page_id, left_header, left_leaf, &rebuilt_left_page);
    if (!status.ok()) {
        *out_merged = false;
        return Status::Ok();
    }
    status = WritePage(rebuilt_left_page);
    if (!status.ok()) {
        return status;
    }

    parent_internal.separator_keys.erase(parent_internal.separator_keys.begin() +
                                         static_cast<std::ptrdiff_t>(parent_link.child_index));
    parent_internal.child_page_ids.erase(parent_internal.child_page_ids.begin() +
                                         static_cast<std::ptrdiff_t>(parent_link.child_index + 1U));

    if (parent_header.is_root && parent_internal.separator_keys.empty()) {
        left_header.parent_page_id = InvalidPageId();
        left_header.is_root = true;
        Page promoted_root_page;
        status = BuildLeafPage(leaf_page_id, left_header, left_leaf, &promoted_root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(promoted_root_page);
        if (!status.ok()) {
            return status;
        }

        metadata_.root_page_id = leaf_page_id;
        metadata_.first_leaf_page_id = leaf_page_id;
        metadata_.tree_height = 1;
        status = WriteMetadata();
        if (!status.ok()) {
            return status;
        }
        *out_merged = true;
        return Status::Ok();
    }

    Page rebuilt_parent_page;
    status = BuildInternalPage(parent_link.page_id, parent_header, parent_internal, &rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }

    *out_merged = true;
    if (!parent_header.is_root && parent_internal.separator_keys.size() < MinKeysPerNonRootNode()) {
        std::vector<SearchPathEntry> parent_path = path;
        parent_path.pop_back();
        return RebalanceAfterDelete(parent_link.page_id, parent_path);
    }
    return Status::Ok();
}

Status BStarTree::MergeInternalWithRightSibling(PageId internal_page_id,
                                                const std::vector<SearchPathEntry>& path,
                                                bool* out_merged) {
    if (out_merged == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_merged must not be nullptr");
    }
    *out_merged = false;
    if (path.empty()) {
        return Status::Ok();
    }

    const SearchPathEntry& parent_link = path.back();
    Page parent_page;
    Status status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }
    if (parent_link.child_index + 1U >= parent_internal.child_page_ids.size()) {
        return Status::Ok();
    }

    const PageId right_page_id = parent_internal.child_page_ids[parent_link.child_index + 1U];
    Page left_page;
    status = ReadPage(internal_page_id, &left_page);
    if (!status.ok()) {
        return status;
    }
    Page right_page;
    status = ReadPage(right_page_id, &right_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader left_header;
    InternalNodeData left_internal;
    status = ParseInternalPage(left_page, &left_header, &left_internal);
    if (!status.ok()) {
        return status;
    }
    NodeHeader right_header;
    InternalNodeData right_internal;
    status = ParseInternalPage(right_page, &right_header, &right_internal);
    if (!status.ok()) {
        return status;
    }

    left_internal.separator_keys.push_back(parent_internal.separator_keys[parent_link.child_index]);
    left_internal.separator_keys.insert(left_internal.separator_keys.end(),
                                        right_internal.separator_keys.begin(),
                                        right_internal.separator_keys.end());
    left_internal.child_page_ids.insert(left_internal.child_page_ids.end(),
                                        right_internal.child_page_ids.begin(),
                                        right_internal.child_page_ids.end());

    Page rebuilt_left_page;
    status = BuildInternalPage(internal_page_id, left_header, left_internal, &rebuilt_left_page);
    if (!status.ok()) {
        *out_merged = false;
        return Status::Ok();
    }
    status = WritePage(rebuilt_left_page);
    if (!status.ok()) {
        return status;
    }

    auto rewrite_child_parent = [&](const InternalNodeData& node_data, PageId parent_page_id) -> Status {
        for (PageId child_page_id : node_data.child_page_ids) {
            Page child_page;
            Status child_status = ReadPage(child_page_id, &child_page);
            if (!child_status.ok()) {
                return child_status;
            }

            NodeHeader child_header;
            child_status = DeserializeNodeHeader(child_page.data, &child_header);
            if (!child_status.ok()) {
                return child_status;
            }
            child_header.parent_page_id = parent_page_id;

            if (child_header.type == NodeType::kLeaf) {
                LeafNodeData child_leaf;
                child_status = ParseLeafPage(child_page, &child_header, &child_leaf);
                if (!child_status.ok()) {
                    return child_status;
                }
                Page rebuilt_child_page;
                child_status = BuildLeafPage(child_page_id, child_header, child_leaf, &rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                child_status = WritePage(rebuilt_child_page);
                if (!child_status.ok()) {
                    return child_status;
                }
                continue;
            }

            InternalNodeData child_internal;
            child_status = ParseInternalPage(child_page, &child_header, &child_internal);
            if (!child_status.ok()) {
                return child_status;
            }
            Page rebuilt_child_page;
            child_status = BuildInternalPage(child_page_id, child_header, child_internal, &rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
            child_status = WritePage(rebuilt_child_page);
            if (!child_status.ok()) {
                return child_status;
            }
        }
        return Status::Ok();
    };

    status = rewrite_child_parent(left_internal, internal_page_id);
    if (!status.ok()) {
        return status;
    }

    parent_internal.separator_keys.erase(parent_internal.separator_keys.begin() +
                                         static_cast<std::ptrdiff_t>(parent_link.child_index));
    parent_internal.child_page_ids.erase(parent_internal.child_page_ids.begin() +
                                         static_cast<std::ptrdiff_t>(parent_link.child_index + 1U));

    if (parent_header.is_root && parent_internal.separator_keys.empty()) {
        left_header.parent_page_id = InvalidPageId();
        left_header.is_root = true;
        Page promoted_root_page;
        status = BuildInternalPage(internal_page_id, left_header, left_internal, &promoted_root_page);
        if (!status.ok()) {
            return status;
        }
        status = WritePage(promoted_root_page);
        if (!status.ok()) {
            return status;
        }

        metadata_.root_page_id = internal_page_id;
        if (metadata_.tree_height > 0U) {
            metadata_.tree_height -= 1U;
        }
        status = WriteMetadata();
        if (!status.ok()) {
            return status;
        }
        *out_merged = true;
        return Status::Ok();
    }

    Page rebuilt_parent_page;
    status = BuildInternalPage(parent_link.page_id, parent_header, parent_internal, &rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }
    status = WritePage(rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }

    *out_merged = true;
    if (!parent_header.is_root && parent_internal.separator_keys.size() < MinKeysPerNonRootNode()) {
        std::vector<SearchPathEntry> parent_path = path;
        parent_path.pop_back();
        return RebalanceAfterDelete(parent_link.page_id, parent_path);
    }
    return Status::Ok();
}

Status BStarTree::UpdateParentSeparatorAfterChildChange(const std::vector<SearchPathEntry>& path,
                                                        const Value& replacement_key) {
    if (path.empty()) {
        return Status::Ok();
    }

    const SearchPathEntry& parent_link = path.back();
    if (parent_link.child_index == 0U) {
        std::vector<SearchPathEntry> parent_path = path;
        parent_path.pop_back();
        return UpdateParentSeparatorAfterChildChange(parent_path, replacement_key);
    }

    Page parent_page;
    Status status = ReadPage(parent_link.page_id, &parent_page);
    if (!status.ok()) {
        return status;
    }

    NodeHeader parent_header;
    InternalNodeData parent_internal;
    status = ParseInternalPage(parent_page, &parent_header, &parent_internal);
    if (!status.ok()) {
        return status;
    }

    parent_internal.separator_keys[parent_link.child_index - 1U] = replacement_key;
    Page rebuilt_parent_page;
    status = BuildInternalPage(parent_link.page_id, parent_header, parent_internal, &rebuilt_parent_page);
    if (!status.ok()) {
        return status;
    }
    return WritePage(rebuilt_parent_page);
}

}
