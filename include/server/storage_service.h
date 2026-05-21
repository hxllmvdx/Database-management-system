#pragma once
#include <unordered_map>
#include "../network/request.h"
#include "../network/response.h"
#include "../network/session.h"
#include "query_processor.h"

namespace db {

class StorageService {
public:
    explicit StorageService(QueryProcessor* processor);

    Response HandleRequest(const Session& session, const Request& request);

private:
    QueryProcessor* processor_;
    std::unordered_map<std::string, SessionContext> session_contexts_;
};

}
