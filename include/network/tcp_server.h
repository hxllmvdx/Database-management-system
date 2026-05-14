#pragma once
#include <functional>
#include <memory>
#include <string>
#include "../common/status.h"
#include "request.h"
#include "response.h"
#include "session.h"

namespace db {

class TcpServer {
public:
    using Handler = std::function<Response(const Session&, const Request&)>;

    TcpServer(std::string host, int port);
    ~TcpServer(); // нужен для pimpl с unique_ptr

    Status Start(Handler handler);
    Status Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_; // указатель на реализацию (pimpl)
};

}
