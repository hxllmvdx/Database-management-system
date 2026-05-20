#include "execution/update_executor.h"

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

db::QueryResult db::UpdateExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const UpdatePhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::UpdateExecutor::Execute(const UpdatePhysicalPlan& plan,
                                       ExecutorContext* ctx,
                                       QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid update executor context");
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
        Tuple updated = row.tuple;
        for (const auto& assignment : plan.assignments) {
            const int index = table.schema.FindColumn(assignment.first);
            if (index < 0) {
                return Status::Error(StatusCode::kExecutionError,
                                     "unknown UPDATE column '" + assignment.first + "'");
            }
            const ColumnSchema& column = table.schema.columns[static_cast<std::size_t>(index)];
            if (assignment.second.is_null() && (column.not_null || column.indexed)) {
                return Status::Error(StatusCode::kConstraintViolation,
                                     "column '" + column.name + "' cannot be NULL");
            }
            updated.values[static_cast<std::size_t>(index)] = assignment.second;
        }
        status = ctx->engine->Update(plan.database_name, plan.table_name, row.rid, updated);
        if (!status.ok()) return status;
        ++result.affected_rows;
    }
    *out = std::move(result);
    return Status::Ok();
}
