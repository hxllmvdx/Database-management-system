#pragma once
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include "../catalog/index_descriptor.h"
#include "../catalog/schema.h"
#include "../parser/ast.h"
#include "../parser/sql_statement.h"
#include "../execution/value.h"

namespace db {

enum class LogicalPlanType {
    kCreateDatabase,
    kDropDatabase,
    kDropTable,
    kUseDatabase,
    kSeqScan,
    kIndexScan,
    kInsert,
    kUpdate,
    kDelete,
    kCreateTable,
    kCreateIndex,
    kRevert,
};

struct LogicalPlan {
    virtual ~LogicalPlan() = default;
    virtual LogicalPlanType type() const = 0;
};

struct LogicalSeqScanPlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    bool select_all = false;
    std::vector<SelectItem> columns;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kSeqScan; }
};

struct LogicalIndexScanPlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    std::string index_name;
    bool select_all = false;
    std::vector<SelectItem> columns;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kIndexScan; }
};

struct LogicalInsertPlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    LogicalPlanType type() const override { return LogicalPlanType::kInsert; }
};

struct LogicalUpdatePlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kUpdate; }
};

struct LogicalDeletePlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kDelete; }
};

struct LogicalCreateTablePlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    TableSchema schema;
    LogicalPlanType type() const override { return LogicalPlanType::kCreateTable; }
};

struct LogicalCreateIndexPlan : LogicalPlan {
    IndexDescriptor descriptor;
    ValueType key_type = ValueType::kNull;
    LogicalPlanType type() const override { return LogicalPlanType::kCreateIndex; }
};

struct LogicalCreateDatabasePlan : LogicalPlan {
    std::string database_name;
    LogicalPlanType type() const override { return LogicalPlanType::kCreateDatabase; }
};

struct LogicalDropDatabasePlan : LogicalPlan {
    std::string database_name;
    LogicalPlanType type() const override { return LogicalPlanType::kDropDatabase; }
};

struct LogicalDropTablePlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    LogicalPlanType type() const override { return LogicalPlanType::kDropTable; }
};

struct LogicalUseDatabasePlan : LogicalPlan {
    std::string database_name;
    LogicalPlanType type() const override { return LogicalPlanType::kUseDatabase; }
};

struct LogicalRevertPlan : LogicalPlan {
    std::string database_name;
    std::string table_name;
    std::int64_t timestamp_ms = 0;
    LogicalPlanType type() const override { return LogicalPlanType::kRevert; }
};

}
