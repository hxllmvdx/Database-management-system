#include "execution/insert_executor.h"

#include <unordered_set>

namespace {

db::QueryResult FromStatus(const db::Status& status) {
    db::QueryResult result;
    if (!status.ok()) {
        result.ok = false;
        result.error = status.message();
    }
    return result;
}

db::Status BuildTuple(const db::TableDescriptor& table,
                      const std::vector<std::string>& columns,
                      const std::vector<db::Value>& values,
                      db::Tuple* out) {
    if (columns.size() != values.size()) {
        return db::Status::Error(db::StatusCode::kExecutionError,
                                 "INSERT values count does not match columns count");
    }
    out->values.assign(table.schema.columns.size(), db::Value::Null());
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (!seen.insert(columns[i]).second) {
            return db::Status::Error(db::StatusCode::kExecutionError,
                                     "duplicate INSERT column '" + columns[i] + "'");
        }
        const int index = table.schema.FindColumn(columns[i]);
        if (index < 0) {
            return db::Status::Error(db::StatusCode::kExecutionError,
                                     "unknown INSERT column '" + columns[i] + "'");
        }
        out->values[static_cast<std::size_t>(index)] = values[i];
    }
    for (std::size_t i = 0; i < table.schema.columns.size(); ++i) {
        const db::ColumnSchema& column = table.schema.columns[i];
        if (out->values[i].is_null() && column.default_value.has_value()) {
            out->values[i] = *column.default_value;
        }
        if (out->values[i].is_null() && (column.not_null || column.indexed)) {
            return db::Status::Error(db::StatusCode::kConstraintViolation,
                                     "column '" + column.name + "' cannot be NULL");
        }
    }
    return db::Status::Ok();
}

}

db::QueryResult db::InsertExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const InsertPhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::InsertExecutor::Execute(const InsertPhysicalPlan& plan,
                                       ExecutorContext* ctx,
                                       QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid insert executor context");
    }
    TableDescriptor table;
    Status status = ctx->engine->GetTableDescriptor(plan.database_name, plan.table_name, &table);
    if (!status.ok()) return status;

    QueryResult result;
    for (const std::vector<Value>& values : plan.rows) {
        Tuple tuple;
        status = BuildTuple(table, plan.columns, values, &tuple);
        if (!status.ok()) return status;
        RowId rid;
        status = ctx->engine->Insert(plan.database_name, plan.table_name, tuple, &rid);
        if (!status.ok()) return status;
        for (const IndexDescriptor& index : table.indexes) {
            const int column_index = table.schema.FindColumn(index.column_name);
            if (column_index >= 0) {
                status = ctx->engine->index_manager().Insert(
                    index.name,
                    tuple.values[static_cast<std::size_t>(column_index)],
                    rid);
                if (!status.ok()) return status;
            }
        }
        ++result.affected_rows;
    }
    *out = std::move(result);
    return Status::Ok();
}
