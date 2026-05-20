#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "bytes.h"
#include "status.h"

namespace db::binary_io {

void WriteBytes(ByteBuffer* buffer, const void* data, std::size_t size);
void WriteUint8(ByteBuffer* buffer, std::uint8_t value);
void WriteUint32(ByteBuffer* buffer, std::uint32_t value);
void WriteUint64(ByteBuffer* buffer, std::uint64_t value);
void WriteInt64(ByteBuffer* buffer, std::int64_t value);
void WriteBool(ByteBuffer* buffer, bool value);
void WriteString(ByteBuffer* buffer, const std::string& value);

Status EnsureReadable(const ByteBuffer& bytes, std::size_t offset, std::size_t need);
Status ReadUint8(const ByteBuffer& bytes, std::size_t* offset, std::uint8_t* out);
Status ReadUint32(const ByteBuffer& bytes, std::size_t* offset, std::uint32_t* out);
Status ReadUint64(const ByteBuffer& bytes, std::size_t* offset, std::uint64_t* out);
Status ReadInt64(const ByteBuffer& bytes, std::size_t* offset, std::int64_t* out);
Status ReadBool(const ByteBuffer& bytes, std::size_t* offset, bool* out);
Status ReadString(const ByteBuffer& bytes, std::size_t* offset, std::string* out);

}
