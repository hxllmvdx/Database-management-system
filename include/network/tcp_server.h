#pragma once
#include <functional> // std::function для обработчика
#include <memory>     // std::unique_ptr для pimpl
#include <string>     // стандартный строковый тип
#include "../common/status.h" // коды возврата операций
#include "request.h"  // формат входящего запроса
#include "response.h" // формат исходящего ответа
#include "session.h"  // транспортные метаданные клиента

namespace db { // пространство имён базы данных

class TcpServer { // tcp-сервер, принимает соединения клиентов
public:
    using Handler = std::function<Response(const Session&, const Request&)>; // тип обработчика

    TcpServer(std::string host, int port); // адрес и порт для прослушивания
    ~TcpServer(); // нужен для pimpl с unique_ptr

    Status Start(Handler handler); // запускает сервер в фоновом потоке
    Status Stop();                 // останавливает сервер и ждёт потоки

private:
    struct Impl; // реализация скрыта (pimpl-идиома)
    std::unique_ptr<Impl> impl_; // указатель на скрытую реализацию
}; // для каждого клиента создаётся поток, session генерируется автоматически

} // namespace db
