#include "auth/rbac.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace db {

Status ParsePermission(const std::string& name, Permission* out) {
    if (name == "READ")          { *out = Permission::kRead;         return Status::Ok(); }
    if (name == "WRITE")         { *out = Permission::kWrite;        return Status::Ok(); }
    if (name == "CREATE_TABLE")  { *out = Permission::kCreateTable;  return Status::Ok(); }
    if (name == "DROP_TABLE")    { *out = Permission::kDropTable;    return Status::Ok(); }
    if (name == "DROP_DATABASE") { *out = Permission::kDropDatabase; return Status::Ok(); }
    if (name == "ALTER_TABLE")   { *out = Permission::kAlterTable;   return Status::Ok(); }
    if (name == "ADMIN")         { *out = Permission::kAdmin;        return Status::Ok(); }
    return Status::Error(StatusCode::kInvalidArgument,
                         "unknown permission: " + name);
}

std::string PermissionName(Permission perm) {
    switch (perm) {
        case Permission::kRead:         return "READ";
        case Permission::kWrite:        return "WRITE";
        case Permission::kCreateTable:  return "CREATE_TABLE";
        case Permission::kDropTable:    return "DROP_TABLE";
        case Permission::kDropDatabase: return "DROP_DATABASE";
        case Permission::kAlterTable:   return "ALTER_TABLE";
        case Permission::kAdmin:        return "ADMIN";
        default:                        return "UNKNOWN";
    }
}

namespace {

constexpr int kSaltBytes       = 16;
constexpr int kHashBytes       = 32;
constexpr int kPbkdf2Iters     = 260000;

std::string HexEncode(const unsigned char* data, int len) {
    std::ostringstream ss;
    ss << std::hex;
    for (int i = 0; i < len; ++i) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return ss.str();
}

bool HexDecode(const std::string& hex, std::vector<unsigned char>* out) {
    if (hex.size() % 2 != 0) return false;
    out->resize(hex.size() / 2);
    for (std::size_t i = 0; i < out->size(); ++i) {
        char buf[3] = {hex[2*i], hex[2*i+1], 0};
        char* end   = nullptr;
        (*out)[i]   = static_cast<unsigned char>(std::strtoul(buf, &end, 16));
        if (end != buf + 2) return false;
    }
    return true;
}

std::string HashPassword(const std::string& password) {
    unsigned char salt[kSaltBytes];
    RAND_bytes(salt, kSaltBytes);

    unsigned char hash[kHashBytes];
    PKCS5_PBKDF2_HMAC(password.c_str(),
                      static_cast<int>(password.size()),
                      salt, kSaltBytes,
                      kPbkdf2Iters,
                      EVP_sha256(),
                      kHashBytes, hash);

    return "pbkdf2$" + std::to_string(kPbkdf2Iters) + "$" +
           HexEncode(salt, kSaltBytes) + "$" +
           HexEncode(hash, kHashBytes);
}

bool TimingSafeEqual(const unsigned char* a, const unsigned char* b, int len) {
    unsigned char diff = 0;
    for (int i = 0; i < len; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

bool VerifyPassword(const std::string& password, const std::string& stored) {
    
    const std::string prefix = "pbkdf2$";
    if (stored.substr(0, prefix.size()) != prefix) return false;

    std::istringstream ss(stored.substr(prefix.size()));
    std::string iters_str, salt_hex, hash_hex;
    if (!std::getline(ss, iters_str, '$')) return false;
    if (!std::getline(ss, salt_hex, '$'))  return false;
    if (!std::getline(ss, hash_hex, '$'))  return false;

    const int iters = std::stoi(iters_str);

    std::vector<unsigned char> salt, stored_hash;
    if (!HexDecode(salt_hex, &salt))        return false;
    if (!HexDecode(hash_hex, &stored_hash)) return false;

    unsigned char computed[kHashBytes];
    PKCS5_PBKDF2_HMAC(password.c_str(),
                      static_cast<int>(password.size()),
                      salt.data(), static_cast<int>(salt.size()),
                      iters, EVP_sha256(),
                      kHashBytes, computed);

    if (stored_hash.size() != kHashBytes) return false;
    return TimingSafeEqual(computed, stored_hash.data(), kHashBytes);
}

json UserToJson(const UserRecord& u) {
    json j;
    j["user_id"]       = u.user_id;
    j["password_hash"] = u.password_hash;
    j["permissions"]   = u.permissions;
    j["groups"]        = u.groups;
    return j;
}

UserRecord UserFromJson(const json& j) {
    UserRecord u;
    u.user_id       = j.at("user_id").get<std::string>();
    u.password_hash = j.at("password_hash").get<std::string>();
    u.permissions   = j.at("permissions").get<uint32_t>();
    for (const auto& g : j.at("groups")) {
        u.groups.insert(g.get<std::string>());
    }
    return u;
}

json GroupToJson(const GroupRecord& g) {
    json j;
    j["name"]        = g.name;
    j["permissions"] = g.permissions;
    return j;
}

GroupRecord GroupFromJson(const json& j) {
    GroupRecord g;
    g.name        = j.at("name").get<std::string>();
    g.permissions = j.at("permissions").get<uint32_t>();
    return g;
}

}  

RbacStore::RbacStore(std::string store_path)
    : store_path_(std::move(store_path)) {}

Status RbacStore::Open() {
    std::unique_lock<std::mutex> lock(mu_);
    fs::create_directories(fs::path(store_path_).parent_path());
    if (fs::exists(store_path_)) {
        return Load();
    }
    
    UserRecord admin;
    admin.user_id       = "admin";
    admin.password_hash = HashPassword("admin");
    admin.permissions   = static_cast<uint32_t>(Permission::kAdmin);
    users_["admin"] = admin;
    return Save();
}

Status RbacStore::Flush() {
    std::unique_lock<std::mutex> lock(mu_);
    return Save();
}

Status RbacStore::Load() {
    std::ifstream f(store_path_);
    if (!f.is_open()) {
        return Status::Error(StatusCode::kIoError,
                             "cannot open rbac store: " + store_path_);
    }
    try {
        const json j = json::parse(f);
        users_.clear();
        groups_.clear();
        for (const auto& u : j.at("users")) {
            UserRecord ur = UserFromJson(u);
            users_[ur.user_id] = ur;
        }
        for (const auto& g : j.at("groups")) {
            GroupRecord gr = GroupFromJson(g);
            groups_[gr.name] = gr;
        }
    } catch (const json::exception& e) {
        return Status::Error(StatusCode::kIoError,
                             std::string("rbac parse error: ") + e.what());
    }
    return Status::Ok();
}

Status RbacStore::Save() const {
    json j;
    json users_arr = json::array();
    for (const auto& [id, u] : users_) {
        users_arr.push_back(UserToJson(u));
    }
    json groups_arr = json::array();
    for (const auto& [name, g] : groups_) {
        groups_arr.push_back(GroupToJson(g));
    }
    j["users"]  = users_arr;
    j["groups"] = groups_arr;

    
    const std::string tmp = store_path_ + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f.is_open()) {
            return Status::Error(StatusCode::kIoError,
                                 "cannot write rbac store: " + tmp);
        }
        f << j.dump(2);
    }
    std::error_code ec;
    fs::rename(tmp, store_path_, ec);
    if (ec) {
        return Status::Error(StatusCode::kIoError,
                             "cannot rename rbac store: " + ec.message());
    }
    return Status::Ok();
}

Status RbacStore::CreateUser(const std::string& user_id,
                             const std::string& plaintext_password) {
    std::unique_lock<std::mutex> lock(mu_);
    if (users_.count(user_id)) {
        return Status::Error(StatusCode::kAlreadyExists,
                             "user already exists: " + user_id);
    }
    UserRecord u;
    u.user_id       = user_id;
    u.password_hash = HashPassword(plaintext_password);
    users_[user_id] = u;
    return Save();
}

Status RbacStore::DropUser(const std::string& user_id) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!users_.count(user_id)) {
        return Status::Error(StatusCode::kNotFound, "user not found: " + user_id);
    }
    users_.erase(user_id);
    return Save();
}

Status RbacStore::CheckPassword(const std::string& user_id,
                                const std::string& plaintext_password,
                                bool* ok) const {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        *ok = false;
        return Status::Ok();
    }
    *ok = VerifyPassword(plaintext_password, it->second.password_hash);
    return Status::Ok();
}

Status RbacStore::CreateGroup(const std::string& group_name) {
    std::unique_lock<std::mutex> lock(mu_);
    if (groups_.count(group_name)) {
        return Status::Error(StatusCode::kAlreadyExists,
                             "group already exists: " + group_name);
    }
    groups_[group_name] = GroupRecord{group_name, 0};
    return Save();
}

Status RbacStore::DropGroup(const std::string& group_name) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!groups_.count(group_name)) {
        return Status::Error(StatusCode::kNotFound,
                             "group not found: " + group_name);
    }
    groups_.erase(group_name);
    for (auto& [id, u] : users_) {
        u.groups.erase(group_name);
    }
    return Save();
}

Status RbacStore::AddUserToGroup(const std::string& user_id,
                                 const std::string& group_name) {
    std::unique_lock<std::mutex> lock(mu_);
    if (!users_.count(user_id)) {
        return Status::Error(StatusCode::kNotFound, "user not found: " + user_id);
    }
    if (!groups_.count(group_name)) {
        return Status::Error(StatusCode::kNotFound,
                             "group not found: " + group_name);
    }
    users_.at(user_id).groups.insert(group_name);
    return Save();
}

Status RbacStore::RemoveUserFromGroup(const std::string& user_id,
                                      const std::string& group_name) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return Status::Error(StatusCode::kNotFound, "user not found: " + user_id);
    }
    it->second.groups.erase(group_name);
    return Save();
}

Status RbacStore::GrantToUser(const std::string& user_id, Permission perm) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return Status::Error(StatusCode::kNotFound, "user not found: " + user_id);
    }
    it->second.permissions |= static_cast<uint32_t>(perm);
    return Save();
}

Status RbacStore::GrantToGroup(const std::string& group_name, Permission perm) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = groups_.find(group_name);
    if (it == groups_.end()) {
        return Status::Error(StatusCode::kNotFound,
                             "group not found: " + group_name);
    }
    it->second.permissions |= static_cast<uint32_t>(perm);
    return Save();
}

Status RbacStore::RevokeFromUser(const std::string& user_id, Permission perm) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return Status::Error(StatusCode::kNotFound, "user not found: " + user_id);
    }
    it->second.permissions &= ~static_cast<uint32_t>(perm);
    return Save();
}

Status RbacStore::RevokeFromGroup(const std::string& group_name, Permission perm) {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = groups_.find(group_name);
    if (it == groups_.end()) {
        return Status::Error(StatusCode::kNotFound,
                             "group not found: " + group_name);
    }
    it->second.permissions &= ~static_cast<uint32_t>(perm);
    return Save();
}

bool RbacStore::HasPermission(const std::string& user_id, Permission perm) const {
    std::unique_lock<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) return false;

    const uint32_t mask = static_cast<uint32_t>(perm);

    
    if (it->second.permissions == static_cast<uint32_t>(Permission::kAdmin))
        return true;

    
    if (it->second.permissions & mask) return true;

    
    for (const auto& group_name : it->second.groups) {
        auto git = groups_.find(group_name);
        if (git == groups_.end()) continue;
        if (git->second.permissions == static_cast<uint32_t>(Permission::kAdmin))
            return true;
        if (git->second.permissions & mask) return true;
    }
    return false;
}

bool RbacStore::UserExists(const std::string& user_id) const {
    std::unique_lock<std::mutex> lock(mu_);
    return users_.count(user_id) > 0;
}

}  
