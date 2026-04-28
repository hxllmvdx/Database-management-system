#pragma once
#include "row_id.h"
#include "../execution/tuple.h"

namespace db {

struct Row {
    RowId rid;
    Tuple tuple;
    bool deleted = false;
};

}
