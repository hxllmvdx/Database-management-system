#pragma once
#include <cstddef>
#include <vector>

#include "../common/status.h"
#include "../catalog/table_descriptor.h"
#include "row.h"
#include "row_store.h"

namespace db {

class TableStorage {
public:
    TableStorage(TableDescriptor descriptor, std::size_t page_size);

    Status Open();
    Status Insert(const Tuple& tuple, RowId* out_rid);
    Status Get(RowId rid, Row* out);
    Status Update(RowId rid, const Tuple& tuple);
    Status Delete(RowId rid);
    Status Scan(std::vector<Row>* out);

    const TableDescriptor& descriptor() const { return descriptor_; }

private:
    Status PrepareTupleForWrite(const Tuple& input, Tuple* output) const;
    Status ValidateValueAgainstColumn(const Value& value, const ColumnSchema& column) const;

    TableDescriptor descriptor_;
    RowStore row_store_;
    bool is_open_;
};

}
