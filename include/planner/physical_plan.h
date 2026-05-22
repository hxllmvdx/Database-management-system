#pragma once
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include "../catalog/index_descriptor.h"
#include "../catalog/table_descriptor.h"
#include "../execution/value.h"
#include "logical_plan.h"

namespace db {

enum class PhysicalPlanType {
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

struct PhysicalPlan {
    virtual ~PhysicalPlan() = default;
    virtual PhysicalPlanType type() const = 0;
};

struct SeqScanPhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    bool select_all = false;
    std::vector<SelectItem> columns;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kSeqScan; }
};

struct IndexScanPhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    std::string index_name;
    bool select_all = false;
    std::vector<SelectItem> columns;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kIndexScan; }
};

struct InsertPhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    PhysicalPlanType type() const override { return PhysicalPlanType::kInsert; }
};

struct UpdatePhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kUpdate; }
};

struct DeletePhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kDelete; }
};

struct CreateTablePhysicalPlan : PhysicalPlan {
    TableDescriptor descriptor;
    PhysicalPlanType type() const override { return PhysicalPlanType::kCreateTable; }
};

struct CreateIndexPhysicalPlan : PhysicalPlan {
    IndexDescriptor descriptor;
    ValueType key_type = ValueType::kNull;
    PhysicalPlanType type() const override { return PhysicalPlanType::kCreateIndex; }
};

struct CreateDatabasePhysicalPlan : PhysicalPlan {
    std::string database_name;
    PhysicalPlanType type() const override { return PhysicalPlanType::kCreateDatabase; }
};

struct DropDatabasePhysicalPlan : PhysicalPlan {
    std::string database_name;
    PhysicalPlanType type() const override { return PhysicalPlanType::kDropDatabase; }
};

struct DropTablePhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    PhysicalPlanType type() const override { return PhysicalPlanType::kDropTable; }
};

struct UseDatabasePhysicalPlan : PhysicalPlan {
    std::string database_name;
    PhysicalPlanType type() const override { return PhysicalPlanType::kUseDatabase; }
};

struct RevertPhysicalPlan : PhysicalPlan {
    std::string database_name;
    std::string table_name;
    std::int64_t timestamp_ms = 0;
    PhysicalPlanType type() const override { return PhysicalPlanType::kRevert; }
};

}
