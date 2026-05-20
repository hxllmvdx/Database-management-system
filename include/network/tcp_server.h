#pragma once
#include <functional>
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

    Status Start(Handler handler);
    Status Stop();

private:
    std::string host_;
    int port_;
};

}
