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
    kSeqScan,
    kIndexScan,
    kInsert,
    kUpdate,
    kDelete,
    kCreateTable,
    kCreateIndex,
};

struct PhysicalPlan {
    virtual ~PhysicalPlan() = default;
    virtual PhysicalPlanType type() const = 0;
};

struct SeqScanPhysicalPlan : PhysicalPlan {
    std::string table_name;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kSeqScan; }
};

struct IndexScanPhysicalPlan : PhysicalPlan {
    std::string table_name;
    std::string index_name;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kIndexScan; }
};

struct InsertPhysicalPlan : PhysicalPlan {
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    PhysicalPlanType type() const override { return PhysicalPlanType::kInsert; }
};

struct UpdatePhysicalPlan : PhysicalPlan {
    std::string table_name;
    std::vector<std::pair<std::string, Value>> assignments;
    std::unique_ptr<Expr> predicate;
    PhysicalPlanType type() const override { return PhysicalPlanType::kUpdate; }
};

struct DeletePhysicalPlan : PhysicalPlan {
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

}
