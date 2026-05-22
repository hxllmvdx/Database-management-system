#include "execution/executor.h"

#include "execution/create_index_executor.h"
#include "execution/create_table_executor.h"
#include "execution/delete_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/update_executor.h"
#include "versioning/revert.h"

namespace {

db::QueryResult ErrorResult(const std::string& message) {
    db::QueryResult result;
    result.ok = false;
    result.error = message;
    return result;
}

}

db::QueryResult db::Executor::Execute(const PhysicalPlan& plan, ExecutorContext* ctx) {
    if (ctx == nullptr || ctx->engine == nullptr) {
        return ErrorResult("executor context has no storage engine");
    }

    switch (plan.type()) {
        case PhysicalPlanType::kCreateDatabase: {
            const auto& p = static_cast<const CreateDatabasePhysicalPlan&>(plan);
            QueryResult result;
            Status status = ctx->engine->CreateDatabase(p.database_name);
            if (!status.ok()) {
                result.ok = false;
                result.error = status.message();
            }
            return result;
        }
        case PhysicalPlanType::kDropDatabase: {
            const auto& p = static_cast<const DropDatabasePhysicalPlan&>(plan);
            QueryResult result;
            Status status = ctx->engine->DropDatabase(p.database_name);
            if (!status.ok()) {
                result.ok = false;
                result.error = status.message();
            }
            return result;
        }
        case PhysicalPlanType::kUseDatabase: {
            const auto& p = static_cast<const UseDatabasePhysicalPlan&>(plan);
            ctx->current_db = p.database_name;
            return QueryResult{};
        }
        case PhysicalPlanType::kDropTable: {
            const auto& p = static_cast<const DropTablePhysicalPlan&>(plan);
            QueryResult result;
            Status status = ctx->engine->DropTable(p.database_name, p.table_name);
            if (!status.ok()) {
                result.ok = false;
                result.error = status.message();
            }
            return result;
        }
        case PhysicalPlanType::kCreateTable:
            return CreateTableExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kCreateIndex:
            return CreateIndexExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kInsert:
            return InsertExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kSeqScan:
            return SeqScanExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kIndexScan:
            return IndexScanExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kUpdate:
            return UpdateExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kDelete:
            return DeleteExecutor().Execute(plan, ctx);
        case PhysicalPlanType::kRevert: {
            const auto& p = static_cast<const RevertPhysicalPlan&>(plan);
            QueryResult result;
            RevertService revert(ctx->engine);
            Status status = revert.RevertTableToTimestamp(
                p.database_name,
                p.table_name,
                p.timestamp_ms);
            if (!status.ok()) {
                result.ok = false;
                result.error = status.message();
            }
            return result;
        }
    }
    return ErrorResult("unsupported physical plan");
}
