#pragma once
#include <string> // стандартный строковый тип
#include "../common/status.h" // коды возврата операций
#include "request.h"  // формат запроса
#include "response.h" // формат ответа

namespace db { // пространство имён базы данных

class TcpClient { // клиент для отправки sql-запросов на сервер по tcp
public:
    TcpClient(std::string host, int port); // host + порт сервера

    Status Send(const Request& req, Response* out); // отправка запроса и ожидание ответа

private:
    std::string host_; // адрес сервера (например "127.0.0.1")
    int port_;         // порт сервера (например 7000)
}; // использует winsock на windows, создаёт сокет на каждый запрос

} // namespace db
