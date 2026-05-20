#pragma once
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include "../catalog/index_descriptor.h"
#include "../catalog/schema.h"
#include "../parser/ast.h"
#include "../execution/value.h"

namespace db {

enum class LogicalPlanType {
    kSeqScan,
    kIndexScan,
    kInsert,
    kUpdate,
    kDelete,
    kCreateTable,
    kCreateIndex,
};

struct LogicalPlan {
    virtual ~LogicalPlan() = default;
    virtual LogicalPlanType type() const = 0;
};

struct LogicalSeqScanPlan : LogicalPlan {
    std::string table_name;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kSeqScan; }
};

struct LogicalIndexScanPlan : LogicalPlan {
    std::string table_name;
    std::string index_name;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kIndexScan; }
};

struct LogicalInsertPlan : LogicalPlan {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    LogicalPlanType type() const override { return LogicalPlanType::kInsert; }
};

struct LogicalUpdatePlan : LogicalPlan {
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kUpdate; }
};

struct LogicalDeletePlan : LogicalPlan {
    std::string table_name;
    std::unique_ptr<Expr> predicate;
    LogicalPlanType type() const override { return LogicalPlanType::kDelete; }
};

struct LogicalCreateTablePlan : LogicalPlan {
    std::string table_name;
    TableSchema schema;
    LogicalPlanType type() const override { return LogicalPlanType::kCreateTable; }
};

struct LogicalCreateIndexPlan : LogicalPlan {
    IndexDescriptor descriptor;
    ValueType key_type = ValueType::kNull;
    LogicalPlanType type() const override { return LogicalPlanType::kCreateIndex; }
};

}
