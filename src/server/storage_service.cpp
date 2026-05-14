// исходный код написан человеком 1, комментарии добавлены человеком 3
#include "server/storage_service.h" // публичный интерфейс сервиса
#include <unordered_map> // хранилище сессий по client_id
#include <mutex>         // защита хранилища от гонок данных

namespace db { // пространство имён базы данных

namespace { // внутреннее хранилище сессий, общее для всех запросов

std::unordered_map<std::string, SessionContext> g_sessions; // client_id → SessionContext
std::mutex g_sessions_mtx; // мьютекс для потокобезопасного доступа к g_sessions

} // anonymous namespace

StorageService::StorageService(QueryProcessor* processor)
    : processor_(processor) {} // сохраняем указатель на QueryProcessor

Response StorageService::HandleRequest(const Session& session,
                                       const Request& request) {
    Response resp{}; // формируем ответ
    resp.ok = true;  // по умолчанию считаем, что всё хорошо

    SessionContext* ctx = nullptr; // указатель на контекст текущей сессии
    {
        std::lock_guard<std::mutex> lock(g_sessions_mtx); // блокируем хранилище
        auto it = g_sessions.find(session.client_id); // ищем существующую сессию
        if (it == g_sessions.end()) { // новый клиент — создаём контекст
            SessionContext new_ctx; // свежий контекст
            new_ctx.client_id = session.client_id; // копируем client_id из транспорта
            auto [inserted_it, _] = g_sessions.emplace(session.client_id, std::move(new_ctx)); // добавляем
            ctx = &inserted_it->second; // получаем указатель на новый контекст
        } else { // клиент уже был — используем существующий контекст
            ctx = &it->second; // указатель на найденный контекст
        } // if
    } // unlock

    if (!request.database.empty()) { // если в запросе явно указана база данных
        ctx->current_db = request.database; // обновляем текущую бд в контексте
    } // if

    if (!request.auth_token.empty()) { // если передан токен аутентификации
        ctx->authenticated = true; // помечаем как аутентифицированного
        ctx->user_id = request.auth_token; // временно используем токен как user_id (TODO: JWT)
    } // if

    QueryResult result = processor_->Execute(request.sql, ctx); // выполняем sql через QueryProcessor (человек 2)

    resp.ok = result.ok;       // копируем флаг успеха
    resp.error = result.error; // копируем текст ошибки
    resp.result = std::move(result); // переносим результат в ответ
    return resp; // возвращаем сформированный Response
} // HandleRequest

} // namespace db
