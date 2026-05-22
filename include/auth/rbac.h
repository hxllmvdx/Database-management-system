#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../common/status.h"

namespace db {

enum class Permission : uint32_t {
    kRead         = 1 << 0,
    kWrite        = 1 << 1,
    kCreateTable  = 1 << 2,
    kDropTable    = 1 << 3,
    kDropDatabase = 1 << 4,
    kAlterTable   = 1 << 5,
    kAdmin        = 0xFFFFFFFF,  
};

Status ParsePermission(const std::string& name, Permission* out);
std::string PermissionName(Permission perm);

struct UserRecord {
    std::string                      user_id;
    std::string                      password_hash;  
    std::unordered_set<std::string>  groups;         
    uint32_t                         permissions = 0; 
};

struct GroupRecord {
    std::string  name;
    uint32_t     permissions = 0;  
};

class RbacStore {
public:
    explicit RbacStore(std::string store_path);

    
    Status Open();

    
    Status Flush();

    

    
    Status CreateUser(const std::string& user_id,
                      const std::string& plaintext_password);

    Status DropUser(const std::string& user_id);

    
    
    Status CheckPassword(const std::string& user_id,
                         const std::string& plaintext_password,
                         bool* ok) const;

    

    Status CreateGroup(const std::string& group_name);
    Status DropGroup(const std::string& group_name);
    Status AddUserToGroup(const std::string& user_id,
                          const std::string& group_name);
    Status RemoveUserFromGroup(const std::string& user_id,
                               const std::string& group_name);

    

    
    Status GrantToUser(const std::string& user_id, Permission perm);
    
    Status GrantToGroup(const std::string& group_name, Permission perm);
    
    Status RevokeFromUser(const std::string& user_id, Permission perm);
    
    Status RevokeFromGroup(const std::string& group_name, Permission perm);

    

    
    bool HasPermission(const std::string& user_id, Permission perm) const;

    
    bool UserExists(const std::string& user_id) const;

private:
    Status Save() const;
    Status Load();

    std::string store_path_;

    mutable std::mutex                                  mu_;
    std::unordered_map<std::string, UserRecord>         users_;
    std::unordered_map<std::string, GroupRecord>        groups_;
};

}  
