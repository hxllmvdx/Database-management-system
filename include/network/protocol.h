#pragma once
#include <string>
#include "request.h"
#include "response.h"

namespace db {

class Protocol {
public:
    static std::string SerializeRequest(const Request& req);
    static bool DeserializeRequest(const std::string& data, Request* out);

    static std::string SerializeResponse(const Response& resp);
    static bool DeserializeResponse(const std::string& data, Response* out);
};

}
