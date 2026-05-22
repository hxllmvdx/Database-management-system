#include "execution/delete_executor.h"

namespace {

db::QueryResult FromStatus(const db::Status& status) {
    db::QueryResult result;
    if (!status.ok()) {
        result.ok = false;
        result.error = status.message();
    }
    return result;
}

bool Matches(const db::Expr* predicate,
             const db::TableSchema& schema,
             const db::Tuple& tuple,
             db::Status* status) {
    if (predicate == nullptr) return true;
    db::Value value;
    db::ExpressionEvaluator evaluator;
    *status = evaluator.Evaluate(*predicate, schema, tuple, &value);
    if (!status->ok()) return false;
    return value.type() == db::ValueType::kBool && value.AsBool();
}

}

db::QueryResult db::DeleteExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const DeletePhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::DeleteExecutor::Execute(const DeletePhysicalPlan& plan,
                                       ExecutorContext* ctx,
                                       QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid delete executor context");
    }
    TableDescriptor table;
    Status status = ctx->engine->GetTableDescriptor(plan.database_name, plan.table_name, &table);
    if (!status.ok()) return status;

    std::vector<Row> rows;
    status = ctx->engine->ScanTable(plan.database_name, plan.table_name, &rows);
    if (!status.ok()) return status;

    QueryResult result;
    for (const Row& row : rows) {
        if (row.deleted || !Matches(plan.predicate.get(), table.schema, row.tuple, &status)) {
            if (!status.ok()) return status;
            continue;
        }
        status = ctx->engine->Delete(plan.database_name, plan.table_name, row.rid);
        if (!status.ok()) return status;
        ++result.affected_rows;
    }
    *out = std::move(result);
    return Status::Ok();
}
