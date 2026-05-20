#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ast.h"
#include "../catalog/schema.h"

namespace db {

enum class StatementKind {
    kCreateDatabase,
    kDropDatabase,
    kUseDatabase,
    kCreateTable,
    kDropTable,
    kInsert,
    kSelect,
    kUpdate,
    kDelete,
    kRevert,
};

class SqlStatement {
public:
    virtual ~SqlStatement() = default;
    virtual StatementKind kind() const = 0;
};

struct CreateDatabaseStatement : SqlStatement {
    std::string db_name;
    StatementKind kind() const override { return StatementKind::kCreateDatabase; }
};

struct DropDatabaseStatement : SqlStatement {
    std::string db_name;
    StatementKind kind() const override { return StatementKind::kDropDatabase; }
};

struct UseDatabaseStatement : SqlStatement {
    std::string db_name;
    StatementKind kind() const override { return StatementKind::kUseDatabase; }
};

struct CreateTableStatement : SqlStatement {
    std::string table_name;
    TableSchema schema;
    StatementKind kind() const override { return StatementKind::kCreateTable; }
};

struct DropTableStatement : SqlStatement {
    std::string table_name;
    StatementKind kind() const override { return StatementKind::kDropTable; }
};

struct InsertStatement : SqlStatement {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    StatementKind kind() const override { return StatementKind::kInsert; }
};

struct SelectStatement : SqlStatement {
    std::string table_name;
    std::vector<std::string> columns;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kSelect; }
};

struct UpdateStatement : SqlStatement {
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kUpdate; }
};

struct DeleteStatement : SqlStatement {
    std::string table_name;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kDelete; }
};

struct RevertStatement : SqlStatement {
    std::string table_name;
    std::int64_t timestamp_ms = 0;
    StatementKind kind() const override { return StatementKind::kRevert; }
};

}
