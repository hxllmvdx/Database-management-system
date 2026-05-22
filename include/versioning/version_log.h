#pragma once
#include <fstream>
#include <string>
#include <vector>

#include "../common/bytes.h"
#include "../common/status.h"
#include "version_record.h"

namespace db {

class VersionLog {
public:
    explicit VersionLog(std::string file_path);

    Status Open();
    Status Flush();
    Status Append(const VersionRecord& record);
    Status ReadAll(std::vector<VersionRecord>* out);

    const std::string& file_path() const { return file_path_; }

private:
    Status InitializeLogFile();
    Status ValidateLogHeader();
    Status WriteLogHeader();
    Status ValidateRecord(const VersionRecord& record) const;
    Status SerializeRecord(const VersionRecord& record, ByteBuffer* out) const;
    Status DeserializeRecord(const ByteBuffer& bytes, std::size_t* offset, VersionRecord* out) const;

    std::string file_path_;
    std::fstream file_;
    bool is_open_ = false;
};

}
