#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "page_manager.h"
#include "../common/status.h"
#include "row.h"

namespace db {

class RowStore {
public:
    explicit RowStore(std::string file_path, std::size_t page_size);

    Status Open();
    Status Flush();

    Status Insert(Row row, RowId* out_rid);
    Status Get(RowId rid, Row* out);
    Status Update(RowId rid, const Tuple& tuple);
    Status Delete(RowId rid);
    Status Scan(std::vector<Row>* out);
    Status Restore(const Row& row);

    const std::string& file_path() const { return file_path_; }

    Status GetIncludingDeleted(RowId rid, Row* out);
    Status ScanIncludingDeleted(std::vector<Row>* out);

private:
    Status WriteMetadataPage();
    Status InitializeStore();
    Status LoadAllRows(std::vector<Row>* out);
    Status RewriteAllRows(const std::vector<Row>& rows);
    Status LoadPageRows(PageId page_id, std::vector<Row>* out);
    Status WritePageRows(PageId page_id, const std::vector<Row>& rows);
    Status FindPageWithFreeSpace(std::size_t row_entry_size, PageId* out_page_id);
    Status FindRowIndexById(const std::vector<Row>& rows, RowId rid, std::size_t* out_index) const;

    std::string file_path_;
    std::size_t page_size_;
    PageManager page_manager_;
    bool is_open_;
    std::uint64_t next_row_id_;
    std::uint64_t data_page_count_;
    std::uint64_t active_row_count_;
};

}
