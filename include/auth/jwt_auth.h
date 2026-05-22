#pragma once

#include <string>
#include "../common/config.h"
#include "../common/status.h"

namespace db {

class JwtAuth {
public:
    explicit JwtAuth(const Config& config);

    
    Status IssueToken(const std::string& user_id, std::string* out_token) const;

    
    
    Status ValidateToken(const std::string& token, std::string* out_user_id) const;

private:
    std::string secret_;
    int         ttl_seconds_;
};

}  
