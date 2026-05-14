// исходный код написан человеком 1, комментарии добавлены человеком 3
#pragma once
#include "../network/request.h"  // входящий запрос от клиента
#include "../network/response.h" // исходящий ответ клиенту
#include "../network/session.h"  // транспортные метаданные клиента
#include "query_processor.h"     // обработчик sql-запросов

namespace db { // пространство имён базы данных

class StorageService { // server-side entrypoint: превращает сетевой запрос в sql-ответ
public:
    explicit StorageService(QueryProcessor* processor); // принимает указатель на процессор запросов

    Response HandleRequest(const Session& session, const Request& request); // обработка одного запроса

private:
    QueryProcessor* processor_; // указатель на QueryProcessor (человек 2) для выполнения sql
}; // хранит глобальную мапу client_id → SessionContext между запросами

} // namespace db
