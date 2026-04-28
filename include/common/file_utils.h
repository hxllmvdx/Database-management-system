#pragma once
#include <string>
#include "bytes.h"
#include "status.h"

namespace db::file_utils {

Status EnsureDir(const std::string& path);
Status RemoveFile(const std::string& path);
Status FileExists(const std::string& path, bool* exists);
Status ReadAllBytes(const std::string& path, ByteBuffer* out);
Status WriteAllBytes(const std::string& path, const ByteBuffer& data);

}
