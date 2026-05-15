#include "planner/planner.h"

#include <memory>
#include <string>
#include <utility>

#include "catalog/catalog_manager.h"
#include "planner/binder.h"

namespace {

std::string ResolveDb(const std::string& current_db, const std::string& explicit_db) {
    return explicit_db.empty() ? current_db : explicit_db;
}

std::unique_ptr<db::Expr> CloneExpr(const db::Expr* expr) {
    if (expr == nullptr) {
        return nullptr;
    }
    switch (expr->kind()) {
        case db::ExprKind::kColumnRef: {
            const auto* src = static_cast<const db::ColumnRefExpr*>(expr);
            auto out = std::make_unique<db::ColumnRefExpr>();
            out->column_name = src->column_name;
            return out;
        }
        case db::ExprKind::kLiteral: {
            const auto* src = static_cast<const db::LiteralExpr*>(expr);
            auto out = std::make_unique<db::LiteralExpr>();
            out->value = src->value;
            return out;
        }
        case db::ExprKind::kBinary: {
            const auto* src = static_cast<const db::BinaryExpr*>(expr);
            auto out = std::make_unique<db::BinaryExpr>();
            out->op = src->op;
            out->left = CloneExpr(src->left.get());
            out->right = CloneExpr(src->right.get());
            return out;
        }
        case db::ExprKind::kLogical: {
            const auto* src = static_cast<const db::LogicalExpr*>(expr);
            auto out = std::make_unique<db::LogicalExpr>();
            out->op = src->op;
            out->left = CloneExpr(src->left.get());
            out->right = CloneExpr(src->right.get());
            return out;
        }
        case db::ExprKind::kBetween: {
            const auto* src = static_cast<const db::BetweenExpr*>(expr);
            auto out = std::make_unique<db::BetweenExpr>();
            out->value = CloneExpr(src->value.get());
            out->low = CloneExpr(src->low.get());
            out->high = CloneExpr(src->high.get());
            return out;
        }
        case db::ExprKind::kLike: {
            const auto* src = static_cast<const db::LikeExpr*>(expr);
            auto out = std::make_unique<db::LikeExpr>();
            out->value = CloneExpr(src->value.get());
            out->pattern = CloneExpr(src->pattern.get());
            return out;
        }
    }
    return nullptr;
}

bool IsColumnEqLiteral(const db::Expr* expr, std::string* column_name) {
    if (expr == nullptr || expr->kind() != db::ExprKind::kBinary) {
        return false;
    }
    const auto* binary = static_cast<const db::BinaryExpr*>(expr);
    if (binary->op != db::BinaryOp::kEq) {
        return false;
    }
    if (binary->left->kind() == db::ExprKind::kColumnRef &&
        binary->right->kind() == db::ExprKind::kLiteral) {
        *column_name = static_cast<const db::ColumnRefExpr*>(binary->left.get())->column_name;
        return true;
    }
    if (binary->right->kind() == db::ExprKind::kColumnRef &&
        binary->left->kind() == db::ExprKind::kLiteral) {
        *column_name = static_cast<const db::ColumnRefExpr*>(binary->right.get())->column_name;
        return true;
    }
    return false;
}

bool FindIndexedEquality(const db::Expr* expr,
                         const db::TableDescriptor& table,
                         std::string* index_name) {
    std::string column_name;
    if (IsColumnEqLiteral(expr, &column_name)) {
        for (const db::IndexDescriptor& index : table.indexes) {
            if (index.column_name == column_name) {
                *index_name = index.name;
                return true;
            }
        }
        for (const db::ColumnSchema& column : table.schema.columns) {
            if (column.name == column_name && column.indexed) {
                *index_name = table.table_name + "_" + column.name + "_idx";
                return true;
            }
        }
    }
    if (expr != nullptr && expr->kind() == db::ExprKind::kLogical) {
        const auto* logical = static_cast<const db::LogicalExpr*>(expr);
        if (logical->op == db::LogicalOp::kAnd) {
            return FindIndexedEquality(logical->left.get(), table, index_name) ||
                   FindIndexedEquality(logical->right.get(), table, index_name);
        }
    }
    return false;
}

db::TableDescriptor MakeTableDescriptor(const std::string& db_name,
                                        const db::CreateTableStatement& stmt) {
    db::TableDescriptor desc;
    desc.database_name = db_name;
    desc.table_name = stmt.table_name;
    desc.schema = stmt.schema;
    for (const db::ColumnSchema& column : desc.schema.columns) {
        if (!column.indexed) {
            continue;
        }
        db::IndexDescriptor index;
        index.name = desc.table_name + "_" + column.name + "_idx";
        index.table_name = desc.table_name;
        index.column_name = column.name;
        index.unique = true;
        desc.indexes.push_back(std::move(index));
    }
    return desc;
}

}

db::Planner::Planner(CatalogManager* catalog) : catalog_(catalog) {}

db::Status db::Planner::BuildLogicalPlan(const std::string& current_db,
                                         const SqlStatement& stmt,
                                         std::unique_ptr<LogicalPlan>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kPlanError, "logical plan output is null");
    }
    out->reset();

    TableDescriptor table;
    Binder binder(catalog_);
    Status status = binder.Bind(current_db, stmt, &table);
    if (!status.ok()) {
        return status;
    }

    switch (stmt.kind()) {
        case StatementKind::kCreateDatabase: {
            const auto& s = static_cast<const CreateDatabaseStatement&>(stmt);
            auto plan = std::make_unique<LogicalCreateDatabasePlan>();
            plan->database_name = s.db_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kDropDatabase: {
            const auto& s = static_cast<const DropDatabaseStatement&>(stmt);
            auto plan = std::make_unique<LogicalDropDatabasePlan>();
            plan->database_name = s.db_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kUseDatabase: {
            const auto& s = static_cast<const UseDatabaseStatement&>(stmt);
            auto plan = std::make_unique<LogicalUseDatabasePlan>();
            plan->database_name = s.db_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kCreateTable: {
            const auto& s = static_cast<const CreateTableStatement&>(stmt);
            auto plan = std::make_unique<LogicalCreateTablePlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->schema = s.schema;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kDropTable: {
            const auto& s = static_cast<const DropTableStatement&>(stmt);
            auto plan = std::make_unique<LogicalDropTablePlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kInsert: {
            const auto& s = static_cast<const InsertStatement&>(stmt);
            auto plan = std::make_unique<LogicalInsertPlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->columns = s.columns;
            plan->rows = s.rows;
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kSelect: {
            const auto& s = static_cast<const SelectStatement&>(stmt);
            std::string index_name;
            if (FindIndexedEquality(s.where.get(), table, &index_name)) {
                auto plan = std::make_unique<LogicalIndexScanPlan>();
                plan->database_name = ResolveDb(current_db, s.database_name);
                plan->table_name = s.table_name;
                plan->index_name = index_name;
                plan->select_all = s.select_all;
                plan->columns = s.columns;
                plan->predicate = CloneExpr(s.where.get());
                *out = std::move(plan);
                return Status::Ok();
            }
            auto plan = std::make_unique<LogicalSeqScanPlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->select_all = s.select_all;
            plan->columns = s.columns;
            plan->predicate = CloneExpr(s.where.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kUpdate: {
            const auto& s = static_cast<const UpdateStatement&>(stmt);
            auto plan = std::make_unique<LogicalUpdatePlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->assignments = s.assignments;
            plan->predicate = CloneExpr(s.where.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kDelete: {
            const auto& s = static_cast<const DeleteStatement&>(stmt);
            auto plan = std::make_unique<LogicalDeletePlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->predicate = CloneExpr(s.where.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case StatementKind::kRevert: {
            const auto& s = static_cast<const RevertStatement&>(stmt);
            auto plan = std::make_unique<LogicalRevertPlan>();
            plan->database_name = ResolveDb(current_db, s.database_name);
            plan->table_name = s.table_name;
            plan->timestamp_ms = s.timestamp_ms;
            *out = std::move(plan);
            return Status::Ok();
        }
    }
    return Status::Error(StatusCode::kPlanError, "unsupported statement");
}

db::Status db::Planner::BuildPhysicalPlan(const std::string& current_db,
                                          const SqlStatement& stmt,
                                          std::unique_ptr<PhysicalPlan>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kPlanError, "physical plan output is null");
    }
    out->reset();

    std::unique_ptr<LogicalPlan> logical;
    Status status = BuildLogicalPlan(current_db, stmt, &logical);
    if (!status.ok()) {
        return status;
    }

    switch (logical->type()) {
        case LogicalPlanType::kCreateDatabase: {
            const auto& s = static_cast<const LogicalCreateDatabasePlan&>(*logical);
            auto plan = std::make_unique<CreateDatabasePhysicalPlan>();
            plan->database_name = s.database_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kDropDatabase: {
            const auto& s = static_cast<const LogicalDropDatabasePlan&>(*logical);
            auto plan = std::make_unique<DropDatabasePhysicalPlan>();
            plan->database_name = s.database_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kUseDatabase: {
            const auto& s = static_cast<const LogicalUseDatabasePlan&>(*logical);
            auto plan = std::make_unique<UseDatabasePhysicalPlan>();
            plan->database_name = s.database_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kCreateTable: {
            const auto& s = static_cast<const LogicalCreateTablePlan&>(*logical);
            CreateTableStatement stmt_view;
            stmt_view.database_name = s.database_name;
            stmt_view.table_name = s.table_name;
            stmt_view.schema = s.schema;
            auto plan = std::make_unique<CreateTablePhysicalPlan>();
            plan->descriptor = MakeTableDescriptor(s.database_name, stmt_view);
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kDropTable: {
            const auto& s = static_cast<const LogicalDropTablePlan&>(*logical);
            auto plan = std::make_unique<DropTablePhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kSeqScan: {
            const auto& s = static_cast<const LogicalSeqScanPlan&>(*logical);
            auto plan = std::make_unique<SeqScanPhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->select_all = s.select_all;
            plan->columns = s.columns;
            plan->predicate = CloneExpr(s.predicate.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kIndexScan: {
            const auto& s = static_cast<const LogicalIndexScanPlan&>(*logical);
            auto plan = std::make_unique<IndexScanPhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->index_name = s.index_name;
            plan->select_all = s.select_all;
            plan->columns = s.columns;
            plan->predicate = CloneExpr(s.predicate.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kInsert: {
            const auto& s = static_cast<const LogicalInsertPlan&>(*logical);
            auto plan = std::make_unique<InsertPhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->columns = s.columns;
            plan->rows = s.rows;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kUpdate: {
            const auto& s = static_cast<const LogicalUpdatePlan&>(*logical);
            auto plan = std::make_unique<UpdatePhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->assignments = s.assignments;
            plan->predicate = CloneExpr(s.predicate.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kDelete: {
            const auto& s = static_cast<const LogicalDeletePlan&>(*logical);
            auto plan = std::make_unique<DeletePhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->predicate = CloneExpr(s.predicate.get());
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kRevert: {
            const auto& s = static_cast<const LogicalRevertPlan&>(*logical);
            auto plan = std::make_unique<RevertPhysicalPlan>();
            plan->database_name = s.database_name;
            plan->table_name = s.table_name;
            plan->timestamp_ms = s.timestamp_ms;
            *out = std::move(plan);
            return Status::Ok();
        }
        case LogicalPlanType::kCreateIndex:
            return Status::Error(StatusCode::kPlanError,
                                 "explicit CREATE INDEX is not part of SQL subset");
    }
    return Status::Error(StatusCode::kPlanError, "unsupported logical plan");
}
