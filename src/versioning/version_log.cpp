#include "versioning/version_log.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "common/binary_io.h"
#include "common/file_utils.h"
#include "common/row_serialization.h"

namespace {

constexpr std::array<char, 8> kVersionLogMagic = {'C', 'D', 'B', 'V', 'L', 'O', 'G', '1'};
constexpr std::uint32_t kVersionLogVersion = 1;

std::size_t LogHeaderSize() {
    return kVersionLogMagic.size() + sizeof(std::uint32_t);
}

bool IsValidVersionOp(db::VersionOp op) {
    switch (op) {
    case db::VersionOp::kInsert:
    case db::VersionOp::kUpdate:
    case db::VersionOp::kDelete:
        return true;
    }
    return false;
}

}

db::VersionLog::VersionLog(std::string file_path) : file_path_(std::move(file_path)) {}

db::Status db::VersionLog::Open() {
    if (is_open_) {
        return Status::Ok();
    }
    if (file_path_.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "VersionLog file path is empty");
    }

    bool exists = false;
    Status status = file_utils::FileExists(file_path_, &exists);
    if (!status.ok()) {
        return status;
    }
    if (!exists) {
        status = InitializeLogFile();
        if (!status.ok()) {
            return status;
        }
    }

    file_.open(file_path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_.is_open()) {
        return Status::Error(StatusCode::kIoError, "Failed to open version log file");
    }

    status = ValidateLogHeader();
    if (!status.ok()) {
        file_.close();
        return status;
    }

    is_open_ = true;
    return Status::Ok();
}

db::Status db::VersionLog::Flush() {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "VersionLog is not open");
    }

    file_.flush();
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to flush version log file");
    }
    return Status::Ok();
}

db::Status db::VersionLog::Append(const VersionRecord& record) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "VersionLog is not open");
    }

    Status status = ValidateRecord(record);
    if (!status.ok()) {
        return status;
    }

    ByteBuffer payload;
    status = SerializeRecord(record, &payload);
    if (!status.ok()) {
        return status;
    }
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return Status::Error(StatusCode::kInvalidArgument, "Serialized version record is too large");
    }

    ByteBuffer entry;
    binary_io::WriteUint32(&entry, static_cast<std::uint32_t>(payload.size()));
    entry.insert(entry.end(), payload.begin(), payload.end());

    file_.clear();
    file_.seekp(0, std::ios::end);
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to seek to end of version log");
    }

    file_.write(reinterpret_cast<const char*>(entry.data()), static_cast<std::streamsize>(entry.size()));
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to append version record");
    }

    return Status::Ok();
}

db::Status db::VersionLog::ReadAll(std::vector<VersionRecord>* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "out must not be nullptr");
    }
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "VersionLog is not open");
    }

    out->clear();

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(LogHeaderSize()), std::ios::beg);
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to seek past version log header");
    }

    while (true) {
        std::array<Byte, sizeof(std::uint32_t)> size_bytes{};
        file_.read(reinterpret_cast<char*>(size_bytes.data()),
                   static_cast<std::streamsize>(size_bytes.size()));
        if (file_.gcount() == 0) {
            file_.clear();
            break;
        }
        if (file_.gcount() != static_cast<std::streamsize>(size_bytes.size())) {
            return Status::Error(StatusCode::kInvalidArgument, "Truncated version log record size");
        }

        std::size_t size_offset = 0;
        ByteBuffer size_buffer(size_bytes.begin(), size_bytes.end());
        std::uint32_t record_size = 0;
        Status status = binary_io::ReadUint32(size_buffer, &size_offset, &record_size);
        if (!status.ok()) {
            return status;
        }

        ByteBuffer payload(record_size);
        file_.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (file_.gcount() != static_cast<std::streamsize>(payload.size())) {
            return Status::Error(StatusCode::kInvalidArgument, "Truncated version log record payload");
        }

        VersionRecord record;
        std::size_t offset = 0;
        status = DeserializeRecord(payload, &offset, &record);
        if (!status.ok()) {
            return status;
        }
        if (offset != payload.size()) {
            return Status::Error(StatusCode::kInvalidArgument,
                                 "Version log record has trailing payload bytes");
        }

        out->push_back(std::move(record));
    }

    return Status::Ok();
}

db::Status db::VersionLog::InitializeLogFile() {
    const std::filesystem::path path(file_path_);
    if (path.has_parent_path()) {
        Status status = file_utils::EnsureDir(path.parent_path().string());
        if (!status.ok()) {
            return status;
        }
    }

    std::ofstream file(file_path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Status::Error(StatusCode::kIoError, "Failed to create version log file");
    }

    ByteBuffer header;
    header.insert(header.end(), kVersionLogMagic.begin(), kVersionLogMagic.end());
    binary_io::WriteUint32(&header, kVersionLogVersion);

    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!file) {
        return Status::Error(StatusCode::kIoError, "Failed to write version log header");
    }

    return Status::Ok();
}

db::Status db::VersionLog::ValidateLogHeader() {
    file_.clear();
    file_.seekg(0, std::ios::beg);
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to seek to version log header");
    }

    ByteBuffer header(LogHeaderSize());
    file_.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (file_.gcount() != static_cast<std::streamsize>(header.size())) {
        return Status::Error(StatusCode::kInvalidArgument, "Version log header is truncated");
    }

    if (!std::equal(kVersionLogMagic.begin(), kVersionLogMagic.end(), header.begin())) {
        return Status::Error(StatusCode::kInvalidArgument, "Invalid version log magic");
    }

    std::size_t offset = kVersionLogMagic.size();
    std::uint32_t version = 0;
    Status status = binary_io::ReadUint32(header, &offset, &version);
    if (!status.ok()) {
        return status;
    }
    if (version != kVersionLogVersion) {
        return Status::Error(StatusCode::kInvalidArgument, "Unsupported version log version");
    }

    return Status::Ok();
}

db::Status db::VersionLog::WriteLogHeader() {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "VersionLog is not open");
    }

    ByteBuffer header;
    header.insert(header.end(), kVersionLogMagic.begin(), kVersionLogMagic.end());
    binary_io::WriteUint32(&header, kVersionLogVersion);

    file_.clear();
    file_.seekp(0, std::ios::beg);
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to seek to version log header for write");
    }

    file_.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to write version log header");
    }

    return Status::Ok();
}

db::Status db::VersionLog::ValidateRecord(const VersionRecord& record) const {
    if (record.table_name.empty()) {
        return Status::Error(StatusCode::kInvalidArgument, "Version record table_name must not be empty");
    }
    if (!IsValidVersionOp(record.op)) {
        return Status::Error(StatusCode::kInvalidArgument, "Version record has invalid operation");
    }
    return Status::Ok();
}

db::Status db::VersionLog::SerializeRecord(const VersionRecord& record, ByteBuffer* out) const {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }

    out->clear();
    binary_io::WriteInt64(out, record.timestamp_ms);
    binary_io::WriteString(out, record.table_name);
    binary_io::WriteUint64(out, record.rid.value);
    binary_io::WriteUint32(out, static_cast<std::uint32_t>(record.op));
    row_serialization::SerializeTuple(record.before, out);
    row_serialization::SerializeTuple(record.after, out);
    return Status::Ok();
}

db::Status db::VersionLog::DeserializeRecord(const ByteBuffer& bytes,
                                             std::size_t* offset,
                                             VersionRecord* out) const {
    if (offset == nullptr || out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "offset and out must not be nullptr");
    }

    Status status = binary_io::ReadInt64(bytes, offset, &out->timestamp_ms);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->table_name);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadUint64(bytes, offset, &out->rid.value);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t raw_op = 0;
    status = binary_io::ReadUint32(bytes, offset, &raw_op);
    if (!status.ok()) {
        return status;
    }
    out->op = static_cast<VersionOp>(raw_op);
    if (!IsValidVersionOp(out->op)) {
        return Status::Error(StatusCode::kInvalidArgument, "Version record contains invalid operation");
    }

    status = row_serialization::DeserializeTuple(bytes, offset, &out->before);
    if (!status.ok()) {
        return status;
    }

    status = row_serialization::DeserializeTuple(bytes, offset, &out->after);
    if (!status.ok()) {
        return status;
    }

    return Status::Ok();
}
