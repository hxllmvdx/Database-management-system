#pragma once
#include <optional>
#include <vector>
#include "../common/status.h"
#include "row.h"

namespace db {

class RowStore {
public:
    Status Insert(Row row, RowId* out_rid);
    Status Get(RowId rid, Row* out);
    Status Update(RowId rid, const Tuple& tuple);
    Status Delete(RowId rid);
    Status Scan(std::vector<Row>* out);
};

}
