#include "server/storage_service.h"
#include <unordered_map>
#include <mutex>

namespace db {

namespace {

// хранилище сессий, общее для всех запросов к сервису
std::unordered_map<std::string, SessionContext> g_sessions; // client_id -> context
std::mutex g_sessions_mtx; // защита хранилища

} // anonymous namespace

StorageService::StorageService(QueryProcessor* processor)
    : processor_(processor) {}

Response StorageService::HandleRequest(const Session& session,
                                       const Request& request) {
    Response resp{};
    resp.ok = true;

    // получаем или создаём session_context по client_id
    SessionContext* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_sessions_mtx);
        auto it = g_sessions.find(session.client_id);
        if (it == g_sessions.end()) {
            SessionContext new_ctx;
            new_ctx.client_id = session.client_id;
            auto [inserted_it, _] = g_sessions.emplace(session.client_id, std::move(new_ctx));
            ctx = &inserted_it->second;
        } else {
            ctx = &it->second;
        }
    }

    // если в запросе указана база данных — обновляем контекст
    if (!request.database.empty()) {
        ctx->current_db = request.database;
    }

    // mvp: аутентификация — просто проверяем, что токен не пуст (если передан)
    if (!request.auth_token.empty()) {
        ctx->authenticated = true;
        ctx->user_id = request.auth_token; // временно используем токен как user_id
    }

    // вызываем query processor
    QueryResult result = processor_->Execute(request.sql, ctx);

    resp.ok = result.ok;
    resp.error = result.error;
    resp.result = std::move(result);
    return resp;
}

} // namespace db
