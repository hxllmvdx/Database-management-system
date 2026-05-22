#include "auth/jwt_auth.h"

#include <chrono>
#include <stdexcept>

#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

namespace db {

JwtAuth::JwtAuth(const Config& config)
    : secret_(config.jwt_secret), ttl_seconds_(config.jwt_ttl_seconds) {}

Status JwtAuth::IssueToken(const std::string& user_id,
                           std::string* out_token) const {
    if (out_token == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_token is null");
    }
    try {
        using namespace std::chrono;
        const auto now    = system_clock::now();
        const auto expiry = now + seconds(ttl_seconds_);

        *out_token = jwt::create()
            .set_issuer("coursedb")
            .set_subject(user_id)
            .set_issued_at(now)
            .set_expires_at(expiry)
            .sign(jwt::algorithm::hs256{secret_});

        return Status::Ok();
    } catch (const std::exception& e) {
        return Status::Error(StatusCode::kInternalError,
                             std::string("jwt issue failed: ") + e.what());
    }
}

Status JwtAuth::ValidateToken(const std::string& token,
                              std::string* out_user_id) const {
    if (out_user_id == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out_user_id is null");
    }
    try {
        const auto decoded = jwt::decode(token);

        const auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer("coursedb");

        verifier.verify(decoded);  

        if (!decoded.has_subject()) {
            return Status::Error(StatusCode::kUnauthorized,
                                 "jwt: missing subject claim");
        }
        *out_user_id = decoded.get_subject();
        return Status::Ok();
    } catch (const jwt::error::token_verification_exception& e) {
        return Status::Error(StatusCode::kUnauthorized,
                             std::string("jwt validation failed: ") + e.what());
    } catch (const std::exception& e) {
        return Status::Error(StatusCode::kUnauthorized,
                             std::string("jwt error: ") + e.what());
    }
}

}  
