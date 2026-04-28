#pragma once
#include "../planner/physical_plan.h"
#include "../server/query_result.h"
#include "executor_context.h"
#include "expression.h"

namespace db {

class Executor {
public:
    virtual ~Executor() = default;
    virtual QueryResult Execute(const PhysicalPlan& plan, ExecutorContext* ctx) = 0;
};

}
