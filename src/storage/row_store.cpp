#include "storage/row_store.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <utility>

#include "common/binary_io.h"
#include "common/bytes.h"
#include "common/row_serialization.h"
#include "common/status.h"

namespace {

constexpr std::array<char, 8> kRowStoreMagic = {'C', 'D', 'B', 'R', 'S', 'T', 'R', '1'};
constexpr std::array<char, 8> kRowStoreDataPageMagic = {'C', 'D', 'B', 'R', 'P', 'A', 'G', 'E'};
constexpr std::uint32_t kRowStoreVersion = 1;

struct RowStoreMetadata {
    std::uint32_t version = 0;
    std::uint64_t next_row_id = 0;
    std::uint64_t data_page_count = 0;
    std::uint64_t active_row_count = 0;
};

struct DataPageHeader {
    std::uint32_t version = 0;
    std::uint32_t entry_count = 0;
    std::uint32_t used_bytes = 0;
};

constexpr db::PageId kMetadataPageId{0};

std::size_t MetadataPagePayloadOffset() {
    return kRowStoreMagic.size();
}

std::size_t DataPagePayloadOffset() {
    return kRowStoreDataPageMagic.size();
}

db::Status SerializeMetadata(const RowStoreMetadata& metadata, db::ByteBuffer* out) {
    out->clear();
    out->insert(out->end(), kRowStoreMagic.begin(), kRowStoreMagic.end());
    db::binary_io::WriteUint32(out, metadata.version);
    db::binary_io::WriteUint64(out, metadata.next_row_id);
    db::binary_io::WriteUint64(out, metadata.data_page_count);
    db::binary_io::WriteUint64(out, metadata.active_row_count);
    return db::Status::Ok();
}

db::Status DeserializeMetadata(const db::ByteBuffer& bytes, RowStoreMetadata* out) {
    if (bytes.size() < MetadataPagePayloadOffset()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "RowStore metadata page is smaller than magic");
    }
    if (!std::equal(kRowStoreMagic.begin(), kRowStoreMagic.end(), bytes.begin())) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid RowStore metadata magic");
    }

    std::size_t offset = MetadataPagePayloadOffset();
    db::Status status = db::binary_io::ReadUint32(bytes, &offset, &out->version);
    if (!status.ok()) {
        return status;
    }
    status = db::binary_io::ReadUint64(bytes, &offset, &out->next_row_id);
    if (!status.ok()) {
        return status;
    }
    status = db::binary_io::ReadUint64(bytes, &offset, &out->data_page_count);
    if (!status.ok()) {
        return status;
    }
    status = db::binary_io::ReadUint64(bytes, &offset, &out->active_row_count);
    if (!status.ok()) {
        return status;
    }

    return db::Status::Ok();
}

db::Status SerializeDataPageHeader(const DataPageHeader& header, db::ByteBuffer* out) {
    out->clear();
    out->insert(out->end(), kRowStoreDataPageMagic.begin(), kRowStoreDataPageMagic.end());
    db::binary_io::WriteUint32(out, header.version);
    db::binary_io::WriteUint32(out, header.entry_count);
    db::binary_io::WriteUint32(out, header.used_bytes);
    return db::Status::Ok();
}

db::Status DeserializeDataPageHeader(const db::ByteBuffer& bytes, DataPageHeader* out) {
    if (bytes.size() < DataPagePayloadOffset()) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "RowStore data page is smaller than magic");
    }
    if (!std::equal(kRowStoreDataPageMagic.begin(), kRowStoreDataPageMagic.end(), bytes.begin())) {
        return db::Status::Error(db::StatusCode::kInvalidArgument, "Invalid RowStore data page magic");
    }

    std::size_t offset = DataPagePayloadOffset();
    db::Status status = db::binary_io::ReadUint32(bytes, &offset, &out->version);
    if (!status.ok()) {
        return status;
    }
    status = db::binary_io::ReadUint32(bytes, &offset, &out->entry_count);
    if (!status.ok()) {
        return status;
    }
    status = db::binary_io::ReadUint32(bytes, &offset, &out->used_bytes);
    if (!status.ok()) {
        return status;
    }

    return db::Status::Ok();
}

db::Status SerializeRowEntry(const db::Row& row, db::ByteBuffer* out) {
    db::ByteBuffer row_payload;
    db::row_serialization::SerializeRow(row, &row_payload);

    out->clear();
    db::binary_io::WriteUint32(out, static_cast<std::uint32_t>(row_payload.size()));
    out->insert(out->end(), row_payload.begin(), row_payload.end());
    return db::Status::Ok();
}

db::Status DeserializeRowEntry(const db::ByteBuffer& bytes, std::size_t* offset, db::Row* out) {
    std::uint32_t row_size = 0;
    db::Status status = db::binary_io::ReadUint32(bytes, offset, &row_size);
    if (!status.ok()) {
        return status;
    }

    status = db::binary_io::EnsureReadable(bytes, *offset, row_size);
    if (!status.ok()) {
        return status;
    }

    std::size_t row_offset = *offset;
    status = db::row_serialization::DeserializeRow(bytes, &row_offset, out);
    if (!status.ok()) {
        return status;
    }

    if (row_offset - *offset != row_size) {
        return db::Status::Error(db::StatusCode::kInvalidArgument,
                                 "Row payload size does not match serialized size");
    }

    *offset = row_offset;
    return db::Status::Ok();
}

std::uint64_t DataPageIdFromOrdinal(std::uint64_t ordinal) {
    return ordinal + 1U;
}

}

db::RowStore::RowStore(std::string file_path, std::size_t page_size)
    : file_path_(std::move(file_path)),
      page_size_(page_size),
      page_manager_(file_path_, page_size_),
      is_open_(false),
      next_row_id_(0),
      data_page_count_(0),
      active_row_count_(0) {}

db::Status db::RowStore::Open() {
    if (is_open_) {
        return Status::Ok();
    }

    Status status = page_manager_.Open();
    if (!status.ok()) {
        return status;
    }

    Page metadata_page;
    status = page_manager_.ReadPage(kMetadataPageId, &metadata_page);
    if (!status.ok()) {
        if (status.code() != StatusCode::kNotFound) {
            return status;
        }
        status = InitializeStore();
        if (!status.ok()) {
            return status;
        }
    } else {
        RowStoreMetadata metadata;
        status = DeserializeMetadata(metadata_page.data, &metadata);
        if (!status.ok()) {
            return status;
        }
        if (metadata.version != kRowStoreVersion) {
            return Status::Error(StatusCode::kInvalidArgument, "Unsupported RowStore metadata version");
        }
        next_row_id_ = metadata.next_row_id;
        data_page_count_ = metadata.data_page_count;
        active_row_count_ = metadata.active_row_count;
    }

    is_open_ = true;
    return Status::Ok();
}

db::Status db::RowStore::Flush() {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }
    return page_manager_.Flush();
}

db::Status db::RowStore::Insert(Row row, RowId* out_rid) {
    if (out_rid == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_rid must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    row.rid.value = next_row_id_;
    row.deleted = false;

    ByteBuffer buffer;
    Status status = SerializeRowEntry(row, &buffer);
    if (!status.ok()) {
        return status;
    }

    std::size_t row_entry_size = buffer.size();

    PageId page_id;
    status = FindPageWithFreeSpace(row_entry_size, &page_id);
    if (!status.ok()) {
        return status;
    }

    std::vector<Row> page_rows;
    status = LoadPageRows(page_id, &page_rows);
    if (!status.ok()) {
        return status;
    }

    page_rows.push_back(row);
    status = WritePageRows(page_id, page_rows);
    if (!status.ok()) {
        return status;
    }

    next_row_id_++;
    *out_rid = row.rid;
    active_row_count_++;

    status = WriteMetadataPage();
    if (!status.ok()) {
        return status;
    }

    return Status::Ok();
}

db::Status db::RowStore::Get(RowId rid, Row* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    Status status = GetIncludingDeleted(rid, out);
    if (!status.ok()) {
        return status;
    }

    if (out->deleted) {
        return Status::Error(StatusCode::kNotFound, "Row is deleted");
    }

    return Status::Ok();
}

db::Status db::RowStore::Update(RowId rid, const Tuple& tuple) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<Row> all_rows;
    Status status = LoadAllRows(&all_rows);
    if (!status.ok()) {
        return status;
    }

    std::size_t index;
    status = FindRowIndexById(all_rows, rid, &index);
    if (!status.ok()) {
        return status;
    }

    if (all_rows[index].deleted) {
        return Status::Error(StatusCode::kNotFound, "Row is deleted");
    }

    all_rows[index].tuple = tuple;
    status = RewriteAllRows(all_rows);
    if (!status.ok()) {
        return status;
    }

    return Status::Ok();
}

db::Status db::RowStore::Delete(RowId rid) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<Row> all_rows;
    Status status = LoadAllRows(&all_rows);
    if (!status.ok()) {
        return status;
    }

    std::size_t index;
    status = FindRowIndexById(all_rows, rid, &index);
    if (!status.ok()) {
        return status;
    }

    if (all_rows[index].deleted) {
        return Status::Ok();
    }

    all_rows[index].deleted = true;
    active_row_count_--;
    status = RewriteAllRows(all_rows);
    if (!status.ok()) {
        return status;
    }

    return Status::Ok();
}

db::Status db::RowStore::Scan(std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<Row> all_rows;
    Status status = LoadAllRows(&all_rows);
    if (!status.ok()) {
        return status;
    }

    out->clear();

    for (const Row& row : all_rows) {
        if (!row.deleted) {
            out->push_back(row);
        }
    }

    return Status::Ok();
}

db::Status db::RowStore::Restore(const Row& row) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<Row> all_rows;
    Status status = LoadAllRows(&all_rows);
    if (!status.ok()) {
        return status;
    }

    std::size_t index;
    status = FindRowIndexById(all_rows, row.rid, &index);
    if (status.ok()) {
        if (all_rows[index].deleted) {
            active_row_count_++;
        }
        all_rows[index] = row;
        all_rows[index].deleted = false;
    } else {
        if (status.code() == StatusCode::kNotFound) {
            Row new_row = row;
            new_row.deleted = false;
            active_row_count_++;
            all_rows.push_back(new_row);
        } else {
            return status;
        }
    }

    next_row_id_ = std::max(next_row_id_, row.rid.value + 1);

    return RewriteAllRows(all_rows);
}

db::Status db::RowStore::GetIncludingDeleted(RowId rid, Row* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<Row> rows;
    Status status = LoadAllRows(&rows);
    if (!status.ok()) {
        return status;
    }

    size_t index;
    status = FindRowIndexById(rows, rid, &index);
    if (!status.ok()) {
        return status;
    }

    *out = rows[index];
    return Status::Ok();
}

db::Status db::RowStore::ScanIncludingDeleted(std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    return LoadAllRows(out);
}

db::Status db::RowStore::InitializeStore() {
    PageId metadata_page_id;
    Status status = page_manager_.AllocatePage(&metadata_page_id);
    if (!status.ok()) {
        return status;
    }
    if (metadata_page_id != kMetadataPageId) {
        return Status::Error(StatusCode::kInternalError, "RowStore metadata page must be allocated first");
    }

    RowStoreMetadata metadata;
    metadata.version = kRowStoreVersion;
    metadata.next_row_id = 0;
    metadata.data_page_count = 0;
    metadata.active_row_count = 0;

    ByteBuffer metadata_bytes;
    SerializeMetadata(metadata, &metadata_bytes);
    if (metadata_bytes.size() > page_size_) {
        return Status::Error(StatusCode::kInternalError, "RowStore metadata does not fit into a page");
    }

    Page page;
    page.id = metadata_page_id;
    page.data.assign(page_size_, 0);
    std::copy(metadata_bytes.begin(), metadata_bytes.end(), page.data.begin());

    status = page_manager_.WritePage(page);
    if (!status.ok()) {
        return status;
    }

    next_row_id_ = 0;
    data_page_count_ = 0;
    active_row_count_ = 0;
    return Status::Ok();
}

db::Status db::RowStore::LoadPageRows(PageId page_id, std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output rows vector is null");
    }

    Page page;
    Status status = page_manager_.ReadPage(page_id, &page);
    if (!status.ok()) {
        return status;
    }

    DataPageHeader header;
    status = DeserializeDataPageHeader(page.data, &header);
    if (!status.ok()) {
        return status;
    }
    if (header.version != kRowStoreVersion) {
        return Status::Error(StatusCode::kInvalidArgument, "Unsupported RowStore data page version");
    }

    const std::size_t header_size = DataPagePayloadOffset() + sizeof(std::uint32_t) * 3U;
    if (header_size + header.used_bytes > page.data.size()) {
        return Status::Error(StatusCode::kInvalidArgument, "RowStore data page used bytes exceed page size");
    }

    std::size_t offset = header_size;
    out->clear();
    out->reserve(header.entry_count);
    const std::size_t used_end = header_size + header.used_bytes;
    for (std::uint32_t i = 0; i < header.entry_count; ++i) {
        if (offset >= used_end) {
            return Status::Error(StatusCode::kInvalidArgument,
                                 "RowStore data page ended before all rows were read");
        }

        Row row;
        status = DeserializeRowEntry(page.data, &offset, &row);
        if (!status.ok()) {
            return status;
        }
        out->push_back(std::move(row));
    }

    if (offset != used_end) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "RowStore data page has trailing payload bytes");
    }

    return Status::Ok();
}

db::Status db::RowStore::WritePageRows(PageId page_id, const std::vector<Row>& rows) {
    DataPageHeader header;
    header.version = kRowStoreVersion;
    header.entry_count = static_cast<std::uint32_t>(rows.size());

    ByteBuffer entries_payload;
    for (const Row& row : rows) {
        ByteBuffer row_entry;
        Status status = SerializeRowEntry(row, &row_entry);
        if (!status.ok()) {
            return status;
        }
        entries_payload.insert(entries_payload.end(), row_entry.begin(), row_entry.end());
    }

    header.used_bytes = static_cast<std::uint32_t>(entries_payload.size());

    ByteBuffer header_bytes;
    Status status = SerializeDataPageHeader(header, &header_bytes);
    if (!status.ok()) {
        return status;
    }

    if (header_bytes.size() + entries_payload.size() > page_size_) {
        return Status::Error(StatusCode::kInvalidArgument, "Rows do not fit into a single data page");
    }

    Page page;
    page.id = page_id;
    page.data.assign(page_size_, 0);
    std::copy(header_bytes.begin(), header_bytes.end(), page.data.begin());
    std::copy(entries_payload.begin(),
              entries_payload.end(),
              page.data.begin() + static_cast<std::ptrdiff_t>(header_bytes.size()));

    return page_manager_.WritePage(page);
}

db::Status db::RowStore::WriteMetadataPage() {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    RowStoreMetadata metadata;
    metadata.version = kRowStoreVersion;
    metadata.data_page_count = data_page_count_;
    metadata.active_row_count = active_row_count_;
    metadata.next_row_id = next_row_id_;

    Page page;
    page.id = kMetadataPageId;
    page.data.assign(page_size_, 0);

    ByteBuffer metadata_bytes;
    Status status = SerializeMetadata(metadata, &metadata_bytes);
    if (!status.ok()) {
        return status;
    }
    if (metadata_bytes.size() > page.data.size()) {
        return Status::Error(StatusCode::kInternalError, "RowStore metadata does not fit into metadata page");
    }
    std::copy(metadata_bytes.begin(), metadata_bytes.end(), page.data.begin());

    return page_manager_.WritePage(page);
}

db::Status db::RowStore::LoadAllRows(std::vector<Row>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output rows vector is null");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    out->clear();
    std::vector<Row> page_rows;
    for (std::uint64_t ordinal = 0; ordinal < data_page_count_; ++ordinal) {
        const PageId page_id{DataPageIdFromOrdinal(ordinal)};
        Status status = LoadPageRows(page_id, &page_rows);
        if (!status.ok()) {
            return status;
        }
        out->insert(out->end(), page_rows.begin(), page_rows.end());
    }

    return Status::Ok();
}

db::Status db::RowStore::RewriteAllRows(const std::vector<Row>& rows) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    std::vector<std::vector<Row>> page_batches;
    std::vector<Row> current_batch;
    std::size_t current_size = 0;
    const std::size_t page_header_size = DataPagePayloadOffset() + sizeof(std::uint32_t) * 3U;

    for (const Row& row : rows) {
        ByteBuffer row_entry;
        Status status = SerializeRowEntry(row, &row_entry);
        if (!status.ok()) {
            return status;
        }

        if (page_header_size + row_entry.size() > page_size_) {
            return Status::Error(StatusCode::kInvalidArgument, "Serialized row is too large for a data page");
        }

        if (!current_batch.empty() && page_header_size + current_size + row_entry.size() > page_size_) {
            page_batches.push_back(current_batch);
            current_batch.clear();
            current_size = 0;
        }

        current_batch.push_back(row);
        current_size += row_entry.size();
    }

    if (!current_batch.empty()) {
        page_batches.push_back(std::move(current_batch));
    }

    while (data_page_count_ < page_batches.size()) {
        PageId page_id;
        Status status = page_manager_.AllocatePage(&page_id);
        if (!status.ok()) {
            return status;
        }
        ++data_page_count_;
    }

    for (std::size_t i = 0; i < data_page_count_; ++i) {
        const PageId page_id{DataPageIdFromOrdinal(static_cast<std::uint64_t>(i))};
        if (i < page_batches.size()) {
            Status status = WritePageRows(page_id, page_batches[i]);
            if (!status.ok()) {
                return status;
            }
        } else {
            Status status = WritePageRows(page_id, {});
            if (!status.ok()) {
                return status;
            }
        }
    }

    RowStoreMetadata metadata;
    metadata.version = kRowStoreVersion;
    metadata.next_row_id = next_row_id_;
    metadata.data_page_count = data_page_count_;
    metadata.active_row_count = active_row_count_;

    ByteBuffer metadata_bytes;
    SerializeMetadata(metadata, &metadata_bytes);
    if (metadata_bytes.size() > page_size_) {
        return Status::Error(StatusCode::kInternalError, "RowStore metadata does not fit into a page");
    }

    Page metadata_page;
    metadata_page.id = kMetadataPageId;
    metadata_page.data.assign(page_size_, 0);
    std::copy(metadata_bytes.begin(), metadata_bytes.end(), metadata_page.data.begin());
    return page_manager_.WritePage(metadata_page);
}

db::Status db::RowStore::FindPageWithFreeSpace(std::size_t row_entry_size, PageId* out_page_id) {
    if (out_page_id == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output page id is null");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "RowStore is not open");
    }

    const std::size_t page_header_size = DataPagePayloadOffset() + sizeof(std::uint32_t) * 3U;
    if (page_header_size + row_entry_size > page_size_) {
        return Status::Error(StatusCode::kInvalidArgument, "Row entry is too large for a data page");
    }

    for (std::uint64_t ordinal = 0; ordinal < data_page_count_; ++ordinal) {
        Page page;
        const PageId page_id{DataPageIdFromOrdinal(ordinal)};
        Status status = page_manager_.ReadPage(page_id, &page);
        if (!status.ok()) {
            return status;
        }

        DataPageHeader header;
        status = DeserializeDataPageHeader(page.data, &header);
        if (!status.ok()) {
            return status;
        }

        if (page_header_size + header.used_bytes + row_entry_size <= page_size_) {
            *out_page_id = page_id;
            return Status::Ok();
        }
    }

    PageId new_page_id;
    Status status = page_manager_.AllocatePage(&new_page_id);
    if (!status.ok()) {
        return status;
    }

    ++data_page_count_;
    status = WritePageRows(new_page_id, {});
    if (!status.ok()) {
        return status;
    }

    *out_page_id = new_page_id;
    return Status::Ok();
}

db::Status db::RowStore::FindRowIndexById(const std::vector<Row>& rows,
                                          RowId rid,
                                          std::size_t* out_index) const {
    if (out_index == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Output row index is null");
    }

    const auto it = std::find_if(rows.begin(), rows.end(), [rid](const Row& row) {
        return row.rid == rid;
    });
    if (it == rows.end()) {
        return Status::Error(StatusCode::kNotFound, "Row not found");
    }

    *out_index = static_cast<std::size_t>(std::distance(rows.begin(), it));
    return Status::Ok();
}
