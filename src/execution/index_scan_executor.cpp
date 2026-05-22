#include "execution/index_scan_executor.h"

#include <optional>
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

bool ExtractEqualityKey(const db::Expr* expr, db::Value* key) {
    if (expr == nullptr) return false;
    if (expr->kind() == db::ExprKind::kLogical) {
        const auto* logical = static_cast<const db::LogicalExpr*>(expr);
        return logical->op == db::LogicalOp::kAnd &&
               (ExtractEqualityKey(logical->left.get(), key) ||
                ExtractEqualityKey(logical->right.get(), key));
    }
    if (expr->kind() != db::ExprKind::kBinary) return false;
    const auto* binary = static_cast<const db::BinaryExpr*>(expr);
    if (binary->op != db::BinaryOp::kEq) return false;
    if (binary->left->kind() == db::ExprKind::kColumnRef &&
        binary->right->kind() == db::ExprKind::kLiteral) {
        *key = static_cast<const db::LiteralExpr*>(binary->right.get())->value;
        return true;
    }
    if (binary->right->kind() == db::ExprKind::kColumnRef &&
        binary->left->kind() == db::ExprKind::kLiteral) {
        *key = static_cast<const db::LiteralExpr*>(binary->left.get())->value;
        return true;
    }
    return false;
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

}

db::QueryResult db::IndexScanExecutor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    QueryResult result;
    const Status status = Execute(static_cast<const IndexScanPhysicalPlan&>(plan), ctx, &result);
    return status.ok() ? result : FromStatus(status);
}

db::Status db::IndexScanExecutor::Execute(const IndexScanPhysicalPlan& plan,
                                          ExecutorContext* ctx,
                                          QueryResult* out) {
    if (ctx == nullptr || ctx->engine == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kExecutionError, "invalid index scan executor context");
    }

    TableDescriptor table;
    Status status = ctx->engine->GetTableDescriptor(plan.database_name, plan.table_name, &table);
    if (!status.ok()) return status;

    Value key;
    if (!ExtractEqualityKey(plan.predicate.get(), &key)) {
        return Status::Error(StatusCode::kExecutionError,
                             "IndexScan requires equality predicate with literal key");
    }

    std::optional<RowId> rid;
    status = ctx->engine->index_manager().Find(plan.index_name, key, &rid);
    if (!status.ok()) return status;

    QueryResult result;
    FillColumns(table.schema, plan.select_all, plan.columns, &result);
    if (!rid.has_value()) {
        *out = std::move(result);
        return Status::Ok();
    }

    std::vector<Row> rows;
    status = ctx->engine->ScanTable(plan.database_name, plan.table_name, &rows);
    if (!status.ok()) return status;

    for (const Row& row : rows) {
        if (row.deleted || row.rid != *rid) {
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
