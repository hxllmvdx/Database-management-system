#include "planner/binder.h"

#include <string>
#include <unordered_set>

#include "catalog/catalog_manager.h"

namespace {

std::string ResolveDb(const std::string& current_db, const std::string& explicit_db) {
    return explicit_db.empty() ? current_db : explicit_db;
}

db::Status RequireDb(const std::string& db_name) {
    if (db_name.empty()) {
        return db::Status::Error(db::StatusCode::kBindError, "database is not selected");
    }
    return db::Status::Ok();
}

db::Status CheckValueType(const db::ColumnSchema& column, const db::Value& value) {
    if (value.is_null()) {
        if (column.not_null || column.indexed) {
            return db::Status::Error(db::StatusCode::kConstraintViolation,
                                     "column '" + column.name + "' cannot be NULL");
        }
        return db::Status::Ok();
    }
    if (column.type == db::ColumnType::kInt && value.type() != db::ValueType::kInt) {
        return db::Status::Error(db::StatusCode::kBindError,
                                 "column '" + column.name + "' expects INT");
    }
    if (column.type == db::ColumnType::kString && value.type() != db::ValueType::kString) {
        return db::Status::Error(db::StatusCode::kBindError,
                                 "column '" + column.name + "' expects STRING");
    }
    return db::Status::Ok();
}

db::Status CheckExprColumns(const db::Expr* expr, const db::TableSchema& schema) {
    if (expr == nullptr) {
        return db::Status::Ok();
    }
    switch (expr->kind()) {
        case db::ExprKind::kColumnRef: {
            const auto* column = static_cast<const db::ColumnRefExpr*>(expr);
            if (schema.FindColumn(column->column_name) < 0) {
                return db::Status::Error(db::StatusCode::kBindError,
                                         "unknown column '" + column->column_name + "'");
            }
            return db::Status::Ok();
        }
        case db::ExprKind::kLiteral:
            return db::Status::Ok();
        case db::ExprKind::kBinary: {
            const auto* binary = static_cast<const db::BinaryExpr*>(expr);
            db::Status status = CheckExprColumns(binary->left.get(), schema);
            return status.ok() ? CheckExprColumns(binary->right.get(), schema) : status;
        }
        case db::ExprKind::kLogical: {
            const auto* logical = static_cast<const db::LogicalExpr*>(expr);
            db::Status status = CheckExprColumns(logical->left.get(), schema);
            return status.ok() ? CheckExprColumns(logical->right.get(), schema) : status;
        }
        case db::ExprKind::kBetween: {
            const auto* between = static_cast<const db::BetweenExpr*>(expr);
            db::Status status = CheckExprColumns(between->value.get(), schema);
            if (!status.ok()) return status;
            status = CheckExprColumns(between->low.get(), schema);
            return status.ok() ? CheckExprColumns(between->high.get(), schema) : status;
        }
        case db::ExprKind::kLike: {
            const auto* like = static_cast<const db::LikeExpr*>(expr);
            db::Status status = CheckExprColumns(like->value.get(), schema);
            return status.ok() ? CheckExprColumns(like->pattern.get(), schema) : status;
        }
    }
    return db::Status::Ok();
}

}

db::Binder::Binder(CatalogManager* catalog) : catalog_(catalog) {}

db::Status db::Binder::Bind(const std::string& current_db,
                            const SqlStatement& stmt,
                            TableDescriptor* bound_table) {
    if (catalog_ == nullptr) {
        return Status::Error(StatusCode::kBindError, "catalog is not available");
    }

    if (stmt.kind() == StatementKind::kCreateDatabase ||
        stmt.kind() == StatementKind::kDropDatabase ||
        stmt.kind() == StatementKind::kUseDatabase) {
        return Status::Ok();
    }

    std::string db_name;
    std::string table_name;
    const Expr* where = nullptr;

    switch (stmt.kind()) {
        case StatementKind::kCreateTable: {
            const auto& create = static_cast<const CreateTableStatement&>(stmt);
            db_name = ResolveDb(current_db, create.database_name);
            Status status = RequireDb(db_name);
            if (!status.ok()) return status;
            std::unordered_set<std::string> seen;
            for (const ColumnSchema& column : create.schema.columns) {
                if (!seen.insert(column.name).second) {
                    return Status::Error(StatusCode::kBindError,
                                         "duplicate column '" + column.name + "'");
                }
                if (column.indexed && !column.not_null) {
                    return Status::Error(StatusCode::kBindError,
                                         "INDEXED column must be NOT_NULL");
                }
            }
            return Status::Ok();
        }
        case StatementKind::kDropTable: {
            const auto& drop = static_cast<const DropTableStatement&>(stmt);
            db_name = ResolveDb(current_db, drop.database_name);
            table_name = drop.table_name;
            break;
        }
        case StatementKind::kInsert: {
            const auto& insert = static_cast<const InsertStatement&>(stmt);
            db_name = ResolveDb(current_db, insert.database_name);
            table_name = insert.table_name;
            break;
        }
        case StatementKind::kSelect: {
            const auto& select = static_cast<const SelectStatement&>(stmt);
            db_name = ResolveDb(current_db, select.database_name);
            table_name = select.table_name;
            where = select.where.get();
            break;
        }
        case StatementKind::kUpdate: {
            const auto& update = static_cast<const UpdateStatement&>(stmt);
            db_name = ResolveDb(current_db, update.database_name);
            table_name = update.table_name;
            where = update.where.get();
            break;
        }
        case StatementKind::kDelete: {
            const auto& del = static_cast<const DeleteStatement&>(stmt);
            db_name = ResolveDb(current_db, del.database_name);
            table_name = del.table_name;
            where = del.where.get();
            break;
        }
        case StatementKind::kRevert: {
            const auto& revert = static_cast<const RevertStatement&>(stmt);
            db_name = ResolveDb(current_db, revert.database_name);
            table_name = revert.table_name;
            break;
        }
        default:
            return Status::Ok();
    }

    Status status = RequireDb(db_name);
    if (!status.ok()) return status;

    TableDescriptor table;
    status = catalog_->GetTable(db_name, table_name, &table);
    if (!status.ok()) return status;
    if (bound_table != nullptr) {
        *bound_table = table;
    }

    if (stmt.kind() == StatementKind::kSelect) {
        const auto& select = static_cast<const SelectStatement&>(stmt);
        if (!select.select_all) {
            for (const SelectItem& item : select.columns) {
                if (table.schema.FindColumn(item.column) < 0) {
                    return Status::Error(StatusCode::kBindError,
                                         "unknown column '" + item.column + "'");
                }
            }
        }
    }

    if (stmt.kind() == StatementKind::kInsert) {
        const auto& insert = static_cast<const InsertStatement&>(stmt);
        if (insert.columns.empty()) {
            return Status::Error(StatusCode::kBindError, "INSERT column list is empty");
        }
        for (const auto& row : insert.rows) {
            if (row.size() != insert.columns.size()) {
                return Status::Error(StatusCode::kBindError,
                                     "INSERT values count does not match columns count");
            }
            for (std::size_t i = 0; i < insert.columns.size(); ++i) {
                const int column_index = table.schema.FindColumn(insert.columns[i]);
                if (column_index < 0) {
                    return Status::Error(StatusCode::kBindError,
                                         "unknown column '" + insert.columns[i] + "'");
                }
                status = CheckValueType(table.schema.columns[static_cast<std::size_t>(column_index)],
                                        row[i]);
                if (!status.ok()) return status;
            }
        }
        for (const ColumnSchema& column : table.schema.columns) {
            if (!column.not_null && !column.indexed) continue;
            bool provided = false;
            for (const std::string& name : insert.columns) {
                if (name == column.name) {
                    provided = true;
                    break;
                }
            }
            if (!provided) {
                return Status::Error(StatusCode::kConstraintViolation,
                                     "required column '" + column.name + "' is missing");
            }
        }
    }

    if (stmt.kind() == StatementKind::kUpdate) {
        const auto& update = static_cast<const UpdateStatement&>(stmt);
        for (const auto& assignment : update.assignments) {
            const int column_index = table.schema.FindColumn(assignment.first);
            if (column_index < 0) {
                return Status::Error(StatusCode::kBindError,
                                     "unknown column '" + assignment.first + "'");
            }
            status = CheckValueType(table.schema.columns[static_cast<std::size_t>(column_index)],
                                    assignment.second);
            if (!status.ok()) return status;
        }
    }

    return CheckExprColumns(where, table.schema);
}
