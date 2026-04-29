#include "common/binary_io.h"

#include <cstring>

namespace db::binary_io {

void WriteBytes(ByteBuffer* buffer, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const Byte*>(data);
    buffer->insert(buffer->end(), bytes, bytes + size);
}

void WriteUint8(ByteBuffer* buffer, std::uint8_t value) {
    buffer->push_back(static_cast<Byte>(value));
}

void WriteUint32(ByteBuffer* buffer, std::uint32_t value) {
    WriteBytes(buffer, &value, sizeof(value));
}

void WriteUint64(ByteBuffer* buffer, std::uint64_t value) {
    WriteBytes(buffer, &value, sizeof(value));
}

void WriteInt64(ByteBuffer* buffer, std::int64_t value) {
    WriteBytes(buffer, &value, sizeof(value));
}

void WriteBool(ByteBuffer* buffer, bool value) {
    WriteUint8(buffer, value ? 1U : 0U);
}

void WriteString(ByteBuffer* buffer, const std::string& value) {
    WriteUint32(buffer, static_cast<std::uint32_t>(value.size()));
    WriteBytes(buffer, value.data(), value.size());
}

Status EnsureReadable(const ByteBuffer& bytes, std::size_t offset, std::size_t need) {
    if (offset > bytes.size() || bytes.size() - offset < need) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Corrupted binary payload: unexpected end of buffer");
    }
    return Status::Ok();
}

template <typename T>
Status ReadPod(const ByteBuffer& bytes, std::size_t* offset, T* out) {
    const Status status = EnsureReadable(bytes, *offset, sizeof(T));
    if (!status.ok()) {
        return status;
    }

    std::memcpy(out, bytes.data() + *offset, sizeof(T));
    *offset += sizeof(T);
    return Status::Ok();
}

Status ReadUint8(const ByteBuffer& bytes, std::size_t* offset, std::uint8_t* out) {
    return ReadPod(bytes, offset, out);
}

Status ReadUint32(const ByteBuffer& bytes, std::size_t* offset, std::uint32_t* out) {
    return ReadPod(bytes, offset, out);
}

Status ReadUint64(const ByteBuffer& bytes, std::size_t* offset, std::uint64_t* out) {
    return ReadPod(bytes, offset, out);
}

Status ReadInt64(const ByteBuffer& bytes, std::size_t* offset, std::int64_t* out) {
    return ReadPod(bytes, offset, out);
}

Status ReadBool(const ByteBuffer& bytes, std::size_t* offset, bool* out) {
    std::uint8_t raw = 0;
    Status status = ReadUint8(bytes, offset, &raw);
    if (!status.ok()) {
        return status;
    }
    if (raw > 1U) {
        return Status::Error(StatusCode::kInvalidArgument,
                             "Corrupted binary payload: invalid bool value");
    }
    *out = (raw == 1U);
    return Status::Ok();
}

Status ReadString(const ByteBuffer& bytes, std::size_t* offset, std::string* out) {
    std::uint32_t size = 0;
    Status status = ReadUint32(bytes, offset, &size);
    if (!status.ok()) {
        return status;
    }

    status = EnsureReadable(bytes, *offset, size);
    if (!status.ok()) {
        return status;
    }

    out->assign(reinterpret_cast<const char*>(bytes.data() + *offset), size);
    *offset += size;
    return Status::Ok();
}

}
