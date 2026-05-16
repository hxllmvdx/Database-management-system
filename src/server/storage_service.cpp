#include "server/storage_service.h"                     // точка входа серверной части
#include "server/query_processor.h"                     // исполнение sql
#include "server/session_context.h"                     // состояние сессии

namespace db {

StorageService::StorageService(QueryProcessor* processor) // конструктор
    : processor_(processor) {}                          // запоминаем процессор

Response StorageService::HandleRequest(const Session& session,
                                       const Request& request) { // обработка одного запроса
    Response resp;                                      // готовим пустой ответ
    if (processor_ == nullptr) {                        // защита от нулевого указателя
        resp.ok = false;                                // помечаем ошибкой
        resp.error = "storage service has no query processor"; // поясняем
        return resp;                                    // возвращаем раньше
    }

    SessionContext& ctx = session_contexts_[session.client_id]; // находим или создаём контекст
    if (ctx.client_id.empty()) {                        // первый раз видим клиента
        ctx.client_id = session.client_id;              // фиксируем идентификатор
    }
    if (!request.database.empty()) {                    // клиент явно указал базу
        ctx.current_db = request.database;              // обновляем текущую бд
    }

    QueryResult result = processor_->Execute(request.sql, &ctx); // выполняем sql
    resp.ok = result.ok;                                // копируем флаг успеха
    resp.error = result.error;                          // и текст ошибки
    resp.result = std::move(result);                    // переносим результат
    return resp;                                        // отдаём готовый ответ
}

} // namespace db
