#include "server/query_processor.h"

#include <exception>

#include "execution/executor.h"
#include "parser/parse_error.h"
#include "server/database.h"

namespace {

db::QueryResult ErrorResult(const std::string& prefix, const std::string& message) {
    db::QueryResult result;
    result.ok = false;
    result.error = prefix + message;
    return result;
}

}

db::QueryProcessor::QueryProcessor(Database* db) : db_(db) {}

db::QueryResult db::QueryProcessor::Execute(const std::string& sql,
                                            SessionContext* session) {
    if (db_ == nullptr || session == nullptr) {
        return ErrorResult("[execute] ", "query processor has invalid context");
    }

    try {
        Parser parser;
        std::unique_ptr<SqlStatement> stmt = parser.Parse(sql);

        Planner planner(&db_->engine().catalog());
        std::unique_ptr<PhysicalPlan> plan;
        Status status = planner.BuildPhysicalPlan(session->current_db, *stmt, &plan);
        if (!status.ok()) {
            return ErrorResult("[plan] ", status.message());
        }

        ExecutorContext ctx;
        ctx.current_db = session->current_db;
        ctx.engine = &db_->engine();

        Executor executor;
        QueryResult result = executor.Execute(*plan, &ctx);
        session->current_db = ctx.current_db;
        return result;
    } catch (const ParseError& error) {
        return ErrorResult("[parse] ", error.what());
    } catch (const std::exception& error) {
        return ErrorResult("[execute] ", error.what());
    }
}
