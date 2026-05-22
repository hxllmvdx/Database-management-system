#include "execution/create_index_executor.h"

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

db::QueryResult db::CreateIndexExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const CreateIndexPhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::CreateIndexExecutor::Execute(const CreateIndexPhysicalPlan& plan,
                                            ExecutorContext* ctx,
                                            QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid create index executor context");
    }
    Status status = ctx->engine->index_manager().CreateIndex(plan.descriptor, plan.key_type);
    if (!status.ok()) {
        return status;
    }
    *out = QueryResult{};
    return Status::Ok();
}
