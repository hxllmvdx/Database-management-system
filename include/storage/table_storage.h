#pragma once
#include <optional>
#include <vector>
#include "../common/status.h"
#include "../catalog/table_descriptor.h"
#include "row.h"

namespace db {

class TableStorage {
public:
    explicit TableStorage(TableDescriptor descriptor);

    Status Open();
    Status Insert(const Tuple& tuple, RowId* out_rid);
    Status Get(RowId rid, Row* out);
    Status Update(RowId rid, const Tuple& tuple);
    Status Delete(RowId rid);
    Status Scan(std::vector<Row>* out);

    const TableDescriptor& descriptor() const { return descriptor_; }

private:
    TableDescriptor descriptor_;
};

}
