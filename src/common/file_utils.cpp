#include "../include/common/file_utils.h"
#include "common/status.h"
#include <filesystem>
#include <fstream>
#include <ios>



db::Status db::file_utils::EnsureDir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return db::Status::Error(db::StatusCode::kIoError, ec.message());
    }
    return db::Status::Ok();
}

db::Status db::file_utils::RemoveFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        return db::Status::Error(db::StatusCode::kInternalError, ec.message());
    }
    return db::Status::Ok();
}

db::Status db::file_utils::FileExists(const std::string &path, bool *exists) {
    std::error_code ec;
    *exists = std::filesystem::exists(path, ec);
    if (ec) {
        return db::Status::Error(db::StatusCode::kInternalError, ec.message());
    }
    return db::Status::Ok();
}

db::Status db::file_utils::ReadAllBytes(const std::string &path, ByteBuffer *out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to open file");
    }

    file.seekg(0, std::ios::end);
    if (!file) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to seek to end of file");
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to get file size");
    }

    file.seekg(0, std::ios::beg);
    if (!file) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to seek to beginning of file");
    }

    out->resize(static_cast<size_t>(size));

    if (size > 0 && !file.read(reinterpret_cast<char*>(out->data()), size)) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to read file");
    }

    return db::Status::Ok();
}

db::Status db::file_utils::WriteAllBytes(const std::string &path, const ByteBuffer &data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to open file");
    }

    if (!file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        return db::Status::Error(db::StatusCode::kInternalError, "Failed to write file");
    }

    return db::Status::Ok();
}
