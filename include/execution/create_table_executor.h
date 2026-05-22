#pragma once
#include "../common/status.h"
#include "executor.h"
#include "executor_context.h"
#include "../planner/physical_plan.h"
#include "../server/query_result.h"

namespace db {

class CreateTableExecutor : public Executor {
public:
    QueryResult Execute(const PhysicalPlan& plan, ExecutorContext* ctx) override;
    Status Execute(const CreateTablePhysicalPlan& plan,
                   ExecutorContext* ctx,
                   QueryResult* out);
};

}
