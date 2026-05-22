#pragma once
#include <string>
#include "../common/status.h"
#include "request.h"
#include "response.h"

namespace db {

class TcpClient {
public:
    TcpClient(std::string host, int port);

    Status Send(const Request& req, Response* out);

private:
    std::string host_;
    int port_;
};

}
