#include "index/key_encoder.h"

#include <cstdint>
#include <string>

#include "common/binary_io.h"

db::Status db::KeyEncoder::Encode(const Value& value, ByteBuffer* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output buffer must not be nullptr");
    }

    out->clear();

    switch (value.type()) {
    case ValueType::kNull:
        return Status::Ok();
    case ValueType::kInt:
        binary_io::WriteInt64(out, value.AsInt());
        return Status::Ok();
    case ValueType::kString: {
        const std::string& string_value = value.AsString();
        out->insert(out->end(), string_value.begin(), string_value.end());
        return Status::Ok();
    }
    case ValueType::kBool:
        binary_io::WriteBool(out, value.AsBool());
        return Status::Ok();
    }

    return Status::Error(StatusCode::kInvalidArgument, "Unsupported value type for key encoding");
}

db::Status db::KeyEncoder::Decode(const ByteBuffer& bytes, ValueType type, Value* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "output value must not be nullptr");
    }

    switch (type) {
    case ValueType::kNull:
        if (!bytes.empty()) {
            return Status::Error(StatusCode::kInvalidArgument, "NULL key payload must be empty");
        }
        *out = Value::Null();
        return Status::Ok();
    case ValueType::kInt: {
        if (bytes.size() != sizeof(std::int64_t)) {
            return Status::Error(StatusCode::kInvalidArgument, "INT key payload must be 8 bytes");
        }
        std::size_t offset = 0;
        std::int64_t value = 0;
        Status status = binary_io::ReadInt64(bytes, &offset, &value);
        if (!status.ok()) {
            return status;
        }
        *out = Value::Int(value);
        return Status::Ok();
    }
    case ValueType::kString:
        *out = Value::String(std::string(bytes.begin(), bytes.end()));
        return Status::Ok();
    case ValueType::kBool: {
        if (bytes.size() != sizeof(std::uint8_t)) {
            return Status::Error(StatusCode::kInvalidArgument, "BOOL key payload must be 1 byte");
        }
        if (bytes[0] != 0U && bytes[0] != 1U) {
            return Status::Error(StatusCode::kInvalidArgument, "BOOL key payload must be 0 or 1");
        }
        *out = Value::Bool(bytes[0] == 1U);
        return Status::Ok();
    }
    }

    return Status::Error(StatusCode::kInvalidArgument, "Unsupported value type for key decoding");
}
