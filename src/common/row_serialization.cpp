#include "common/row_serialization.h"
#include "common/binary_io.h"
#include "common/bytes.h"
#include "common/value_serialization.h"
#include <cstdint>

namespace db::row_serialization {

void SerializeTuple(const Tuple& tuple, ByteBuffer* buffer) {
    std::size_t num_values = tuple.values.size();
    binary_io::WriteUint64(buffer, num_values);

    for (const auto& value : tuple.values) {
        value_serialization::SerializeValue(value, buffer);
    }
}

Status DeserializeTuple(const ByteBuffer& bytes, std::size_t* offset, Tuple* tuple) {
    std::uint64_t num_values = 0;
    Status status = binary_io::ReadUint64(bytes, offset, &num_values);
    if (!status.ok()) {
        return status;
    }

    constexpr std::uint64_t kMaxTupleValues = 1U << 20;
    if (num_values > kMaxTupleValues) {
        return Status::Error(StatusCode::kInvalidArgument, "Tuple value count is unreasonably large");
    }

    tuple->values.clear();
    tuple->values.resize(static_cast<std::size_t>(num_values));
    for (std::size_t i = 0; i < static_cast<std::size_t>(num_values); ++i) {
        status = value_serialization::DeserializeValue(bytes, offset, &tuple->values[i]);
        if (!status.ok()) {
            return status;
        }
    }
    return Status::Ok();
}

void SerializeRow(const Row& row, ByteBuffer* buffer) {
    binary_io::WriteUint64(buffer, row.rid.value);
    binary_io::WriteBool(buffer, row.deleted);

    ByteBuffer temp;
    SerializeTuple(row.tuple, &temp);

    binary_io::WriteUint64(buffer, temp.size());
    buffer->insert(buffer->end(), temp.begin(), temp.end());
}

Status DeserializeRow(const ByteBuffer& bytes, std::size_t* offset, Row* row) {
    std::uint64_t rid_value = 0;
    Status status = binary_io::ReadUint64(bytes, offset, &rid_value);
    if (!status.ok()) {
        return status;
    }

    row->rid.value = rid_value;

    bool deleted = false;
    status = binary_io::ReadBool(bytes, offset, &deleted);
    if (!status.ok()) {
        return status;
    }

    row->deleted = deleted;

    std::uint64_t tuple_size = 0;
    status = binary_io::ReadUint64(bytes, offset, &tuple_size);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::EnsureReadable(bytes, *offset, static_cast<std::size_t>(tuple_size));
    if (!status.ok()) {
        return status;
    }

    std::size_t tuple_offset = *offset;
    status = DeserializeTuple(bytes, &tuple_offset, &row->tuple);
    if (!status.ok()) {
        return status;
    }

    if (tuple_offset - *offset != tuple_size) {
        return Status::Error(StatusCode::kInvalidArgument, "Deserialized tuple size does not match expected size");
    }

    *offset = tuple_offset;
    return status;
}

}
