#pragma once
#include <vector>
#include "value.h"

namespace db {

struct Tuple {
    std::vector<Value> values;
};

}
