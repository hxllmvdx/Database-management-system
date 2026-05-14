#pragma once
#include <string> // стандартный строковый тип
#include "request.h"  // формат запроса клиента
#include "response.h" // формат ответа сервера

namespace db { // пространство имён базы данных

class Protocol { // сериализация и десериализация сетевых сообщений
public:
    static std::string SerializeRequest(const Request& req);      // запрос → байты
    static bool DeserializeRequest(const std::string& data, Request* out); // байты → запрос

    static std::string SerializeResponse(const Response& resp);       // ответ → байты
    static bool DeserializeResponse(const std::string& data, Response* out); // байты → ответ
}; // формат сообщения: 4 байта длины (little-endian) + json-пayload

} // namespace db
