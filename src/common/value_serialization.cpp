#include "common/value_serialization.h"
#include "common/binary_io.h"

namespace db::value_serialization {

    void SerializeValue(const Value& value, ByteBuffer* buffer) {
        binary_io::WriteUint32(buffer, static_cast<std::uint32_t>(value.type()));
        switch (value.type()) {
            case ValueType::kNull:
                break;
            case ValueType::kInt:
                binary_io::WriteInt64(buffer, value.AsInt());
                break;
            case ValueType::kString:
                binary_io::WriteString(buffer, value.AsString());
                break;
            case ValueType::kBool:
                binary_io::WriteBool(buffer, value.AsBool());
                break;
        }
    }

    Status DeserializeValue(const ByteBuffer& bytes, std::size_t* offset, Value* out) {
        std::uint32_t raw_type = 0;
        Status status = binary_io::ReadUint32(bytes, offset, &raw_type);
        if (!status.ok()) {
            return status;
        }

        const auto type = static_cast<ValueType>(raw_type);
        switch (type) {
            case ValueType::kNull:
                *out = Value::Null();
                return Status::Ok();
            case ValueType::kInt: {
                std::int64_t value = 0;
                status = binary_io::ReadInt64(bytes, offset, &value);
                if (!status.ok()) {
                    return status;
                }
                *out = Value::Int(value);
                return Status::Ok();
            }
            case ValueType::kString: {
                std::string value;
                status = binary_io::ReadString(bytes, offset, &value);
                if (!status.ok()) {
                    return status;
                }
                *out = Value::String(std::move(value));
                return Status::Ok();
            }
            case ValueType::kBool: {
                bool value = false;
                status = binary_io::ReadBool(bytes, offset, &value);
                if (!status.ok()) {
                    return status;
                }
                *out = Value::Bool(value);
                return Status::Ok();
            }
        }

        return Status::Error(StatusCode::kInvalidArgument,
                             "Corrupted metadata: invalid value type");
    }

}
