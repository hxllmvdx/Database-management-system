#pragma once
#include <vector>
#include "common/value.h"

namespace db {

struct Tuple {
    std::vector<Value> values;
};

}
