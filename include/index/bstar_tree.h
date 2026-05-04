#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../common/bytes.h"
#include "../common/status.h"
#include "../execution/value.h"
#include "../storage/page_manager.h"
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
    BStarTree(std::string index_file,
              ValueType key_type,
              std::size_t page_size = 4096,
              std::size_t min_degree = 2);

    Status Open();
    Status Insert(const Value& key, RowId rid);
    Status Delete(const Value& key);
    Status Find(const Value& key, std::optional<RowId>* out);
    Status RangeSearch(const KeyRange& range, std::vector<RowId>* out);

private:
    enum class NodeType : std::uint32_t {
        kInternal = 1,
        kLeaf = 2,
    };

    struct TreeMetadata {
        std::uint32_t version = 0;
        PageId root_page_id{};
        PageId first_leaf_page_id{};
        std::uint64_t min_degree = 0;
        std::uint64_t tree_height = 0;
    };

    struct NodeHeader {
        NodeType type = NodeType::kLeaf;
        std::uint32_t key_count = 0;
        PageId parent_page_id{};
        PageId next_leaf_page_id{};
        bool is_root = false;
    };

    struct LeafEntry {
        Value key;
        RowId rid;
    };

    struct LeafNodeData {
        std::vector<LeafEntry> entries;
    };

    struct InternalNodeData {
        std::vector<Value> separator_keys;
        std::vector<PageId> child_page_ids;
    };

    struct SearchPathEntry {
        PageId page_id{};
        std::size_t child_index = 0;
    };

    static constexpr std::uint64_t kInvalidPageIdValue = std::numeric_limits<std::uint64_t>::max();

    Status ValidateKeyType(const Value& key) const;
    Status ValidateRange(const KeyRange& range) const;

    Status InitializeTree();
    Status LoadMetadata();
    Status WriteMetadata();

    Status ReadPage(PageId page_id, Page* out) const;
    Status WritePage(const Page& page);
    Status AllocateNodePage(PageId* out_page_id);

    Status SerializeMetadata(ByteBuffer* out) const;
    Status DeserializeMetadata(const ByteBuffer& bytes, TreeMetadata* out) const;
    Status SerializeNodeHeader(const NodeHeader& header, ByteBuffer* out) const;
    Status DeserializeNodeHeader(const ByteBuffer& bytes, NodeHeader* out) const;

    Status ParseLeafPage(const Page& page, NodeHeader* out_header, LeafNodeData* out_leaf) const;
    Status BuildLeafPage(PageId page_id,
                         const NodeHeader& header,
                         const LeafNodeData& leaf,
                         Page* out_page) const;
    Status ParseInternalPage(const Page& page,
                             NodeHeader* out_header,
                             InternalNodeData* out_internal) const;
    Status BuildInternalPage(PageId page_id,
                             const NodeHeader& header,
                             const InternalNodeData& internal,
                             Page* out_page) const;

    Status SerializeLeafEntry(const LeafEntry& entry, ByteBuffer* out) const;
    Status DeserializeLeafEntry(const ByteBuffer& bytes, std::size_t* offset, LeafEntry* out) const;
    Status SerializeSeparatorKey(const Value& key, ByteBuffer* out) const;
    Status DeserializeSeparatorKey(const ByteBuffer& bytes, std::size_t* offset, Value* out) const;

    Status FindPathToLeaf(const Value& key,
                          std::vector<SearchPathEntry>* out_path,
                          PageId* out_leaf_page_id,
                          std::optional<RowId>* out_existing_rid) const;
    Status FindMinimumKey(PageId subtree_root, Value* out_key) const;
    bool IsLeafUnderfull(const NodeHeader& header, const LeafNodeData& leaf) const;
    bool IsInternalUnderfull(const NodeHeader& header, const InternalNodeData& internal) const;
    std::size_t MaxKeysPerNode() const;
    std::size_t MinKeysPerNonRootNode() const;
    std::size_t FindEntryPosition(const std::vector<LeafEntry>& entries, const Value& key) const;
    std::size_t ChooseChildIndex(const InternalNodeData& internal, const Value& key) const;

    Status InsertIntoLeaf(PageId leaf_page_id,
                          const std::vector<SearchPathEntry>& path,
                          const Value& key,
                          RowId rid);
    Status InsertIntoInternal(PageId internal_page_id,
                              const std::vector<SearchPathEntry>& path,
                              std::size_t left_child_index,
                              const Value& separator_key,
                              PageId right_child_page_id);

    Status SplitLeaf(PageId leaf_page_id,
                     const std::vector<SearchPathEntry>& path,
                     const LeafNodeData& expanded_leaf);
    Status SplitInternal(PageId internal_page_id,
                         const std::vector<SearchPathEntry>& path,
                         const InternalNodeData& expanded_internal);

    Status RedistributeLeafWithRightSibling(PageId leaf_page_id,
                                            const std::vector<SearchPathEntry>& path,
                                            const LeafNodeData& expanded_leaf,
                                            bool* out_redistributed);
    Status RedistributeInternalWithRightSibling(PageId internal_page_id,
                                                const std::vector<SearchPathEntry>& path,
                                                const InternalNodeData& expanded_internal,
                                                bool* out_redistributed);

    Status DeleteFromLeaf(PageId leaf_page_id,
                          const std::vector<SearchPathEntry>& path,
                          const Value& key);
    Status RebalanceAfterDelete(PageId page_id, const std::vector<SearchPathEntry>& path);
    Status MergeLeafWithRightSibling(PageId leaf_page_id,
                                     const std::vector<SearchPathEntry>& path,
                                     bool* out_merged);
    Status MergeInternalWithRightSibling(PageId internal_page_id,
                                         const std::vector<SearchPathEntry>& path,
                                         bool* out_merged);
    Status UpdateParentSeparatorAfterChildChange(const std::vector<SearchPathEntry>& path,
                                                 const Value& replacement_key);

    std::string index_file_;
    ValueType key_type_;
    std::size_t page_size_;
    std::size_t min_degree_;
    mutable PageManager page_manager_;
    bool is_open_ = false;
    TreeMetadata metadata_;
};

}
