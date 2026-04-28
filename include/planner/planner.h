#pragma once
#include <memory>
#include "../common/status.h"
#include "logical_plan.h"
#include "physical_plan.h"
#include "../parser/sql_statement.h"

namespace db {

class CatalogManager;

class Planner {
public:
    explicit Planner(CatalogManager* catalog);

    Status BuildLogicalPlan(const std::string& current_db,
                            const SqlStatement& stmt,
                            std::unique_ptr<LogicalPlan>* out);

    Status BuildPhysicalPlan(const std::string& current_db,
                             const SqlStatement& stmt,
                             std::unique_ptr<PhysicalPlan>* out);

private:
    CatalogManager* catalog_;
};

}
