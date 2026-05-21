#include "execution/create_table_executor.h"

namespace {

db::QueryResult FromStatus(const db::Status& status) {
    db::QueryResult result;
    if (!status.ok()) {
        result.ok = false;
        result.error = status.message();
    }
    return result;
}

}

db::QueryResult db::CreateTableExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const CreateTablePhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::CreateTableExecutor::Execute(const CreateTablePhysicalPlan& plan,
                                            ExecutorContext* ctx,
                                            QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid create table executor context");
    }
    Status status = ctx->engine->CreateTable(plan.descriptor);
    if (!status.ok()) {
        return status;
    }
    *out = QueryResult{};
    return Status::Ok();
}
