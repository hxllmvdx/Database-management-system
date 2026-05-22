#include "server/storage_service.h"

#include <filesystem>
#include <optional>
#include <thread>

#include <nlohmann/json.hpp>

#include "auth/rbac.h"
#include "logging/logger.h"
#include "parser/parse_error.h"
#include "parser/parser.h"
#include "parser/sql_statement.h"
#include "queue/task_queue.h"
#include "server/query_processor.h"
#include "server/session_context.h"

using json = nlohmann::json;

namespace db {

namespace {

QueryResult OkResult(const std::string& msg = "") {
    QueryResult r;
    r.ok = true;
    if (!msg.empty()) {
        r.columns = {"message"};
        Tuple row;
        row.values.push_back(Value::String(msg));
        r.rows.push_back(std::move(row));
    }
    return r;
}

QueryResult ErrResult(const std::string& msg) {
    QueryResult r;
    r.ok    = false;
    r.error = msg;
    return r;
}

}  

StorageService::StorageService(QueryProcessor* processor, const Config& config)
    : processor_(processor)
    , jwt_(config)
    , rbac_((std::filesystem::path(config.data_dir) / "rbac.json").string())
    , task_queue_(config, [this](TaskInfo& task) -> Status {
          
          SessionContext ctx;
          ctx.current_db   = task.database;
          ctx.user_id      = task.user_id;
          ctx.authenticated = true;
          QueryResult result = processor_->Execute(task.sql, &ctx);
          if (!result.ok) {
              return Status::Error(StatusCode::kExecutionError, result.error);
          }
          
          json j;
          j["columns"]       = result.columns;
          j["affected_rows"] = result.affected_rows;
          json rows_arr = json::array();
          for (const auto& row : result.rows) {
              json row_arr = json::array();
              for (const auto& val : row.values) {
                  if (val.is_null()) row_arr.push_back(nullptr);
                  else if (val.type() == ValueType::kInt)
                      row_arr.push_back(val.AsInt());
                  else
                      row_arr.push_back(val.AsString());
              }
              rows_arr.push_back(row_arr);
          }
          j["rows"] = rows_arr;
          task.result = j.dump();
          return Status::Ok();
      })
    , metrics_("local-node", config.telemetry_window_seconds) {

    
    rbac_.Open();
    
    task_queue_.Start();

    Logger::Info("storage service initialised");
}

StorageService::~StorageService() {
    task_queue_.Stop();
}

Response StorageService::HandleRequest(const Session& session,
                                        const Request& request) {
    const int64_t start_ms = Logger::NowMs();

    Response resp;

    
    SessionContext* ctx_ptr = nullptr;
    {
        std::unique_lock<std::mutex> lock(ctx_mu_);
        auto& ctx = session_contexts_[session.client_id];
        if (ctx.client_id.empty()) ctx.client_id = session.client_id;
        if (!request.database.empty()) ctx.current_db = request.database;
        ctx_ptr = &ctx;
    }
    SessionContext& ctx = *ctx_ptr;

    
    
    const auto startsWithLogin = [](const std::string& s) -> bool {
        if (s.size() < 5) return false;
        const std::string prefix = s.substr(0, 5);
        std::string up;
        for (char c : prefix) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return up == "LOGIN";
    };
    const bool is_login = startsWithLogin(request.sql);

    if (!is_login) {
        if (!request.auth_token.empty()) {
            std::string user_id;
            Status vs = jwt_.ValidateToken(request.auth_token, &user_id);
            if (vs.ok()) {
                ctx.authenticated = true;
                ctx.user_id       = user_id;
            } else {
                resp.ok    = false;
                resp.error = "authentication failed: " + vs.message();
                resp.result = ErrResult(resp.error);
                metrics_.RecordRequest(Logger::NowMs() - start_ms, true);
                return resp;
            }
        }
        
        
    }

    
    std::unique_ptr<SqlStatement> stmt;
    try {
        Parser parser;
        stmt = parser.Parse(request.sql);
    } catch (const ParseError& e) {
        resp.ok    = false;
        resp.error = std::string("[parse] ") + e.what();
        resp.result = ErrResult(resp.error);
        metrics_.RecordRequest(Logger::NowMs() - start_ms, true);
        return resp;
    }

    
    const std::optional<Permission> required = RequiredPermission(stmt->kind());
    if (required.has_value() && ctx.authenticated && !ctx.user_id.empty()) {
        if (!rbac_.HasPermission(ctx.user_id, *required)) {
            resp.ok    = false;
            resp.error = "permission denied: " +
                         PermissionName(*required) + " required";
            resp.result = ErrResult(resp.error);
            metrics_.RecordRequest(Logger::NowMs() - start_ms, true);
            return resp;
        }
    }

    
    QueryResult result;

    switch (stmt->kind()) {
        
        case StatementKind::kLogin:
        case StatementKind::kCreateUser:
        case StatementKind::kDropUser:
        case StatementKind::kCreateGroup:
        case StatementKind::kDropGroup:
        case StatementKind::kAddUserToGroup:
        case StatementKind::kGrant:
        case StatementKind::kRevoke:
            result = HandleAuthStatement(*stmt, ctx);
            break;

        
        case StatementKind::kSubmitQuery:
        case StatementKind::kGetTask:
        case StatementKind::kCancelTask:
            result = HandleQueueStatement(*stmt, ctx);
            break;

        
        case StatementKind::kShowMetrics:
            result = HandleShowMetrics();
            break;

        
        default:
            result = processor_->Execute(request.sql, &ctx);
            break;
    }

    resp.ok     = result.ok;
    resp.error  = result.error;
    resp.result = std::move(result);

    const int64_t duration_ms = Logger::NowMs() - start_ms;
    metrics_.RecordRequest(duration_ms, !resp.ok);

    
    AccessLogEntry entry;
    entry.timestamp_start = Logger::NowIso8601();
    entry.timestamp_end   = Logger::NowIso8601();
    entry.request_id      = request.request_id;
    entry.client_id       = session.client_id;
    entry.sql             = request.sql;
    entry.target_node     = "local";
    entry.shard_id        = ctx.current_db;
    entry.duration_ms     = duration_ms;
    entry.ok              = resp.ok;
    entry.error           = resp.error;
    {
        std::ostringstream ss;
        ss << std::this_thread::get_id();
        entry.thread_id = std::stoi(ss.str().substr(0, 8), nullptr, 16);
    }
    Logger::AccessLog(entry);

    return resp;
}

QueryResult StorageService::HandleAuthStatement(const SqlStatement& stmt,
                                                 SessionContext& ctx) {
    switch (stmt.kind()) {
        case StatementKind::kLogin: {
            const auto& s = static_cast<const LoginStatement&>(stmt);
            bool ok = false;
            Status cs = rbac_.CheckPassword(s.user_id, s.password, &ok);
            if (!cs.ok()) return ErrResult(cs.message());
            if (!ok)      return ErrResult("invalid credentials");

            std::string token;
            Status ts = jwt_.IssueToken(s.user_id, &token);
            if (!ts.ok()) return ErrResult(ts.message());

            ctx.authenticated = true;
            ctx.user_id       = s.user_id;

            QueryResult r;
            r.ok = true;
            r.columns = {"token"};
            Tuple row;
            row.values.push_back(Value::String(token));
            r.rows.push_back(std::move(row));
            return r;
        }

        case StatementKind::kCreateUser: {
            const auto& s = static_cast<const CreateUserStatement&>(stmt);
            Status st = rbac_.CreateUser(s.user_id, s.password);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("user created: " + s.user_id);
        }

        case StatementKind::kDropUser: {
            const auto& s = static_cast<const DropUserStatement&>(stmt);
            Status st = rbac_.DropUser(s.user_id);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("user dropped: " + s.user_id);
        }

        case StatementKind::kCreateGroup: {
            const auto& s = static_cast<const CreateGroupStatement&>(stmt);
            Status st = rbac_.CreateGroup(s.group_name);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("group created: " + s.group_name);
        }

        case StatementKind::kDropGroup: {
            const auto& s = static_cast<const DropGroupStatement&>(stmt);
            Status st = rbac_.DropGroup(s.group_name);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("group dropped: " + s.group_name);
        }

        case StatementKind::kAddUserToGroup: {
            const auto& s = static_cast<const AddUserToGroupStatement&>(stmt);
            Status st = rbac_.AddUserToGroup(s.user_id, s.group_name);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("user " + s.user_id + " added to group " + s.group_name);
        }

        case StatementKind::kGrant: {
            const auto& s = static_cast<const GrantStatement&>(stmt);
            Permission perm{};
            Status ps = ParsePermission(s.permission, &perm);
            if (!ps.ok()) return ErrResult(ps.message());
            Status st;
            if (s.target_type == "USER") {
                st = rbac_.GrantToUser(s.target_name, perm);
            } else {
                st = rbac_.GrantToGroup(s.target_name, perm);
            }
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("granted " + s.permission + " to " +
                            s.target_type + " " + s.target_name);
        }

        case StatementKind::kRevoke: {
            const auto& s = static_cast<const RevokeStatement&>(stmt);
            Permission perm{};
            Status ps = ParsePermission(s.permission, &perm);
            if (!ps.ok()) return ErrResult(ps.message());
            Status st;
            if (s.target_type == "USER") {
                st = rbac_.RevokeFromUser(s.target_name, perm);
            } else {
                st = rbac_.RevokeFromGroup(s.target_name, perm);
            }
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("revoked " + s.permission + " from " +
                            s.target_type + " " + s.target_name);
        }

        default:
            return ErrResult("unexpected auth statement kind");
    }
}

QueryResult StorageService::HandleQueueStatement(const SqlStatement& stmt,
                                                   SessionContext& ctx) {
    switch (stmt.kind()) {
        case StatementKind::kSubmitQuery: {
            const auto& s = static_cast<const SubmitQueryStatement&>(stmt);
            std::string request_id;
            Status st = task_queue_.Submit(s.inner_sql, ctx.current_db,
                                           ctx.user_id, &request_id);
            if (!st.ok()) return ErrResult(st.message());

            json j;
            j["request_id"] = request_id;

            QueryResult r;
            r.ok = true;
            r.columns = {"request_id"};
            Tuple row;
            row.values.push_back(Value::String(request_id));
            r.rows.push_back(std::move(row));
            return r;
        }

        case StatementKind::kGetTask: {
            const auto& s = static_cast<const GetTaskStatement&>(stmt);
            TaskInfo info;
            Status st = task_queue_.GetStatus(s.request_id, &info);
            if (!st.ok()) return ErrResult(st.message());

            json j;
            j["request_id"]  = info.request_id;
            j["status"]      = TaskStatusName(info.status);
            j["progress"]    = info.progress;
            j["result"]      = info.result;
            j["error"]       = info.error;
            j["submitted_ms"] = info.submitted_ms;
            j["started_ms"]  = info.started_ms;
            j["finished_ms"] = info.finished_ms;

            QueryResult r;
            r.ok = true;
            r.columns = {"status", "progress", "result", "error"};
            Tuple row;
            row.values.push_back(Value::String(TaskStatusName(info.status)));
            row.values.push_back(Value::Int(info.progress));
            row.values.push_back(Value::String(info.result));
            row.values.push_back(Value::String(info.error));
            r.rows.push_back(std::move(row));
            return r;
        }

        case StatementKind::kCancelTask: {
            const auto& s = static_cast<const CancelTaskStatement&>(stmt);
            Status st = task_queue_.Cancel(s.request_id);
            if (!st.ok()) return ErrResult(st.message());
            return OkResult("task cancelled: " + s.request_id);
        }

        default:
            return ErrResult("unexpected queue statement kind");
    }
}

QueryResult StorageService::HandleShowMetrics() {
    const MetricsSnapshot snap = metrics_.Snapshot();

    json j;
    j["node_id"]         = snap.node_id;
    j["rps_current"]     = snap.rps_current;
    j["rps_avg_10min"]   = snap.rps_avg_10min;
    j["rps_max_10min"]   = snap.rps_max_10min;
    j["latency_avg_ms"]  = snap.latency_avg_ms;
    j["latency_p95_ms"]  = snap.latency_p95_ms;
    j["latency_p99_ms"]  = snap.latency_p99_ms;
    j["error_rate_1min"] = snap.error_rate_1min;
    j["total_requests"]  = snap.total_requests;
    j["total_errors"]    = snap.total_errors;

    QueryResult r;
    r.ok = true;
    r.columns = {"metrics"};
    Tuple row;
    row.values.push_back(Value::String(j.dump(2)));
    r.rows.push_back(std::move(row));
    return r;
}

std::optional<Permission> StorageService::RequiredPermission(StatementKind kind) {
    switch (kind) {
        case StatementKind::kSelect:      return Permission::kRead;
        case StatementKind::kInsert:      return Permission::kWrite;
        case StatementKind::kUpdate:      return Permission::kWrite;
        case StatementKind::kDelete:      return Permission::kWrite;
        case StatementKind::kCreateTable: return Permission::kCreateTable;
        case StatementKind::kDropTable:   return Permission::kDropTable;
        case StatementKind::kDropDatabase:return Permission::kDropDatabase;
        case StatementKind::kRevert:      return Permission::kWrite;
        case StatementKind::kSubmitQuery: return Permission::kRead;
        
        default:                          return std::nullopt;
    }
}

}  
