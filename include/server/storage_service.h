#pragma once
#include <memory>
#include <unordered_map>

#include "../auth/jwt_auth.h"
#include "../auth/rbac.h"
#include "../common/config.h"
#include "../network/request.h"
#include "../network/response.h"
#include "../network/session.h"
#include "../queue/task_queue.h"
#include "../telemetry/metrics.h"
#include "query_processor.h"

namespace db {

class StorageService {
public:
    StorageService(QueryProcessor* processor, const Config& config);
    ~StorageService();

    Response HandleRequest(const Session& session, const Request& request);

    
    const NodeMetrics& metrics() const { return metrics_; }

private:
    
    QueryResult HandleAuthStatement(const SqlStatement& stmt,
                                    SessionContext& ctx);
    QueryResult HandleQueueStatement(const SqlStatement& stmt,
                                     SessionContext&    ctx);
    QueryResult HandleShowMetrics();

    
    static std::optional<Permission> RequiredPermission(StatementKind kind);

    QueryProcessor*  processor_;
    JwtAuth          jwt_;
    RbacStore        rbac_;
    TaskQueue        task_queue_;
    NodeMetrics      metrics_;

    std::mutex                                       ctx_mu_;
    std::unordered_map<std::string, SessionContext>  session_contexts_;
};

}  
