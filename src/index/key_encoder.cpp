#include "index/key_encoder.h"

#include <cstdint>
#include <string>

#include "common/binary_io.h"
#include "common/error.h"

db::ByteBuffer db::KeyEncoder::Encode(const Value& value) {
    ByteBuffer bytes;

    switch (value.type()) {
    case ValueType::kNull:
        return bytes;
    case ValueType::kInt:
        binary_io::WriteInt64(&bytes, value.AsInt());
        return bytes;
    case ValueType::kString: {
        const std::string& string_value = value.AsString();
        bytes.insert(bytes.end(), string_value.begin(), string_value.end());
        return bytes;
    }
    case ValueType::kBool:
        binary_io::WriteBool(&bytes, value.AsBool());
        return bytes;
    }

    throw DbError(StatusCode::kInvalidArgument, "Unsupported value type for key encoding");
}

db::Value db::KeyEncoder::Decode(const ByteBuffer& bytes, ValueType type) {
    switch (type) {
    case ValueType::kNull:
        if (!bytes.empty()) {
            throw DbError(StatusCode::kInvalidArgument, "NULL key payload must be empty");
        }
        return Value::Null();
    case ValueType::kInt: {
        if (bytes.size() != sizeof(std::int64_t)) {
            throw DbError(StatusCode::kInvalidArgument, "INT key payload must be 8 bytes");
        }
        std::size_t offset = 0;
        std::int64_t value = 0;
        Status status = binary_io::ReadInt64(bytes, &offset, &value);
        if (!status.ok()) {
            throw DbError(status.code(), status.message());
        }
        return Value::Int(value);
    }
    case ValueType::kString:
        return Value::String(std::string(bytes.begin(), bytes.end()));
    case ValueType::kBool: {
        if (bytes.size() != sizeof(std::uint8_t)) {
            throw DbError(StatusCode::kInvalidArgument, "BOOL key payload must be 1 byte");
        }
        if (bytes[0] != 0U && bytes[0] != 1U) {
            throw DbError(StatusCode::kInvalidArgument, "BOOL key payload must be 0 or 1");
        }
        return Value::Bool(bytes[0] == 1U);
    }
    }

    throw DbError(StatusCode::kInvalidArgument, "Unsupported value type for key decoding");
}
