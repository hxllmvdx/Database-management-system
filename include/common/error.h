#pragma once
#include <stdexcept>
#include <string>
#include "status.h"

namespace db {

class DbError : public std::runtime_error {
public:
    DbError(StatusCode code, const std::string& msg)
        : std::runtime_error(msg), code_(code) {}

    StatusCode code() const { return code_; }

private:
    StatusCode code_;
};

}
