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
    
    kCreateUser,
    kDropUser,
    kLogin,
    kCreateGroup,
    kDropGroup,
    kAddUserToGroup,
    kGrant,
    kRevoke,
    
    kSubmitQuery,
    kGetTask,
    kCancelTask,
    
    kShowMetrics,
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
    std::string database_name;
    std::string table_name;
    TableSchema schema;
    StatementKind kind() const override { return StatementKind::kCreateTable; }
};

struct DropTableStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    StatementKind kind() const override { return StatementKind::kDropTable; }
};

struct InsertStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    StatementKind kind() const override { return StatementKind::kInsert; }
};

struct SelectItem {
    std::string column;
    std::string alias;
};

struct SelectStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    bool select_all = false;
    std::vector<SelectItem> columns;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kSelect; }
};

struct UpdateStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kUpdate; }
};

struct DeleteStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    std::unique_ptr<Expr> where;
    StatementKind kind() const override { return StatementKind::kDelete; }
};

struct RevertStatement : SqlStatement {
    std::string database_name;
    std::string table_name;
    std::int64_t timestamp_ms = 0;
    StatementKind kind() const override { return StatementKind::kRevert; }
};

struct CreateUserStatement : SqlStatement {
    std::string user_id;
    std::string password;
    StatementKind kind() const override { return StatementKind::kCreateUser; }
};

struct DropUserStatement : SqlStatement {
    std::string user_id;
    StatementKind kind() const override { return StatementKind::kDropUser; }
};

struct LoginStatement : SqlStatement {
    std::string user_id;
    std::string password;
    StatementKind kind() const override { return StatementKind::kLogin; }
};

struct CreateGroupStatement : SqlStatement {
    std::string group_name;
    StatementKind kind() const override { return StatementKind::kCreateGroup; }
};

struct DropGroupStatement : SqlStatement {
    std::string group_name;
    StatementKind kind() const override { return StatementKind::kDropGroup; }
};

struct AddUserToGroupStatement : SqlStatement {
    std::string user_id;
    std::string group_name;
    StatementKind kind() const override { return StatementKind::kAddUserToGroup; }
};

struct GrantStatement : SqlStatement {
    std::string permission;   
    std::string target_type;  
    std::string target_name;
    StatementKind kind() const override { return StatementKind::kGrant; }
};

struct RevokeStatement : SqlStatement {
    std::string permission;
    std::string target_type;  
    std::string target_name;
    StatementKind kind() const override { return StatementKind::kRevoke; }
};

struct SubmitQueryStatement : SqlStatement {
    std::string inner_sql;   
    StatementKind kind() const override { return StatementKind::kSubmitQuery; }
};

struct GetTaskStatement : SqlStatement {
    std::string request_id;
    StatementKind kind() const override { return StatementKind::kGetTask; }
};

struct CancelTaskStatement : SqlStatement {
    std::string request_id;
    StatementKind kind() const override { return StatementKind::kCancelTask; }
};

struct ShowMetricsStatement : SqlStatement {
    StatementKind kind() const override { return StatementKind::kShowMetrics; }
};

}
