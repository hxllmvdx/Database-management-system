#pragma once
#include <memory>
#include "logical_plan.h"
#include "physical_plan.h"

namespace db {

class Optimizer {
public:
    std::unique_ptr<PhysicalPlan> Optimize(const LogicalPlan& logical);
};

}
