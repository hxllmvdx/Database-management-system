#pragma once
#include <string>

namespace db {

enum class StatusCode {
    kOk = 0,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kConstraintViolation,
    kIoError,
    kParseError,
    kBindError,
    kPlanError,
    kExecutionError,
    kTransactionError,
    kNetworkError,
    kUnauthorized,
    kInternalError,
};

class Status {
public:
    Status() : code_(StatusCode::kOk) {}
    Status(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Status Ok() { return Status(); }
    static Status Error(StatusCode code, std::string message) {
        return Status(code, std::move(message));
    }

    bool ok() const { return code_ == StatusCode::kOk; }
    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }

private:
    StatusCode code_;
    std::string message_;
};

}
