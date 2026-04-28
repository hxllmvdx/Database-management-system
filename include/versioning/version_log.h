#pragma once
#include <string>
#include <vector>
#include "../common/status.h"
#include "version_record.h"

namespace db {

class VersionLog {
public:
    explicit VersionLog(std::string file_path);

    Status Open();
    Status Append(const VersionRecord& record);
    Status ReadAll(std::vector<VersionRecord>* out);

private:
    std::string file_path_;
};

}
