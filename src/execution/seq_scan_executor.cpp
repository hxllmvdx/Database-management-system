#include "execution/seq_scan_executor.h"

#include <string>
#include <vector>

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
    if (predicate == nullptr) {
        return true;
    }
    db::Value value;
    db::ExpressionEvaluator evaluator;
    *status = evaluator.Evaluate(*predicate, schema, tuple, &value);
    if (!status->ok()) {
        return false;
    }
    return value.type() == db::ValueType::kBool && value.AsBool();
}

void ProjectRow(const db::TableSchema& schema,
                const db::Tuple& tuple,
                bool select_all,
                const std::vector<db::SelectItem>& columns,
                db::QueryResult* out) {
    db::Tuple projected;
    if (select_all) {
        projected = tuple;
    } else {
        for (const db::SelectItem& item : columns) {
            const int index = schema.FindColumn(item.column);
            projected.values.push_back(tuple.values[static_cast<std::size_t>(index)]);
        }
    }
    out->rows.push_back(std::move(projected));
}

void FillColumns(const db::TableSchema& schema,
                 bool select_all,
                 const std::vector<db::SelectItem>& columns,
                 db::QueryResult* out) {
    if (select_all) {
        for (const db::ColumnSchema& column : schema.columns) {
            out->columns.push_back(column.name);
        }
        return;
    }
    for (const db::SelectItem& item : columns) {
        out->columns.push_back(item.alias.empty() ? item.column : item.alias);
    }
}

}

db::QueryResult db::SeqScanExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const SeqScanPhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::SeqScanExecutor::Execute(const SeqScanPhysicalPlan& plan,
                                        ExecutorContext* ctx,
                                        QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid seq scan executor context");
    }
    TableDescriptor table;
    Status status = ctx->engine->GetTableDescriptor(plan.database_name, plan.table_name, &table);
    if (!status.ok()) return status;

    std::vector<Row> rows;
    status = ctx->engine->ScanTable(plan.database_name, plan.table_name, &rows);
    if (!status.ok()) return status;

    QueryResult result;
    FillColumns(table.schema, plan.select_all, plan.columns, &result);
    for (const Row& row : rows) {
        if (row.deleted) {
            continue;
        }
        if (Matches(plan.predicate.get(), table.schema, row.tuple, &status)) {
            ProjectRow(table.schema, row.tuple, plan.select_all, plan.columns, &result);
        }
        if (!status.ok()) return status;
    }
    result.affected_rows = result.rows.size();
    *out = std::move(result);
    return Status::Ok();
}
