#include "common/catalog_serialization.h"
#include "common/value_serialization.h"
#include <utility>
#include "common/binary_io.h"

namespace db::catalog_serialization {

void SerializeColumnSchema(const ColumnSchema& column, ByteBuffer* buffer) {
    binary_io::WriteString(buffer, column.name);
    binary_io::WriteUint32(buffer, static_cast<std::uint32_t>(column.type));
    binary_io::WriteBool(buffer, column.not_null);
    binary_io::WriteBool(buffer, column.indexed);
    binary_io::WriteBool(buffer, column.default_value.has_value());
    if (column.default_value.has_value()) {
        db::value_serialization::SerializeValue(*column.default_value, buffer);
    }
}

Status DeserializeColumnSchema(const ByteBuffer& bytes, std::size_t* offset, ColumnSchema* out) {
    Status status = binary_io::ReadString(bytes, offset, &out->name);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t raw_type = 0;
    status = binary_io::ReadUint32(bytes, offset, &raw_type);
    if (!status.ok()) {
        return status;
    }
    out->type = static_cast<ColumnType>(raw_type);

    status = binary_io::ReadBool(bytes, offset, &out->not_null);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadBool(bytes, offset, &out->indexed);
    if (!status.ok()) {
        return status;
    }

    bool has_default = false;
    status = binary_io::ReadBool(bytes, offset, &has_default);
    if (!status.ok()) {
        return status;
    }

    out->default_value.reset();
    if (has_default) {
        Value value;
        status = db::value_serialization::DeserializeValue(bytes, offset, &value);
        if (!status.ok()) {
            return status;
        }
        out->default_value = std::move(value);
    }

    return Status::Ok();
}

void SerializeTableSchema(const TableSchema& schema, ByteBuffer* buffer) {
    binary_io::WriteUint32(buffer, static_cast<std::uint32_t>(schema.columns.size()));
    for (const ColumnSchema& column : schema.columns) {
        SerializeColumnSchema(column, buffer);
    }
}

Status DeserializeTableSchema(const ByteBuffer& bytes, std::size_t* offset, TableSchema* out) {
    std::uint32_t column_count = 0;
    Status status = binary_io::ReadUint32(bytes, offset, &column_count);
    if (!status.ok()) {
        return status;
    }

    out->columns.clear();
    out->columns.reserve(column_count);
    for (std::uint32_t i = 0; i < column_count; ++i) {
        ColumnSchema column;
        status = DeserializeColumnSchema(bytes, offset, &column);
        if (!status.ok()) {
            return status;
        }
        out->columns.push_back(std::move(column));
    }

    return Status::Ok();
}

void SerializeIndexDescriptor(const IndexDescriptor& index, ByteBuffer* buffer) {
    binary_io::WriteString(buffer, index.name);
    binary_io::WriteString(buffer, index.table_name);
    binary_io::WriteString(buffer, index.column_name);
    binary_io::WriteBool(buffer, index.unique);
}

Status DeserializeIndexDescriptor(const ByteBuffer& bytes, std::size_t* offset, IndexDescriptor* out) {
    Status status = binary_io::ReadString(bytes, offset, &out->name);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->table_name);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->column_name);
    if (!status.ok()) {
        return status;
    }

    return binary_io::ReadBool(bytes, offset, &out->unique);
}

void SerializeTableDescriptor(const TableDescriptor& desc, ByteBuffer* buffer) {
    binary_io::WriteString(buffer, desc.database_name);
    binary_io::WriteString(buffer, desc.table_name);
    binary_io::WriteString(buffer, desc.data_file);
    binary_io::WriteString(buffer, desc.index_file);
    SerializeTableSchema(desc.schema, buffer);
    binary_io::WriteUint32(buffer, static_cast<std::uint32_t>(desc.indexes.size()));
    for (const IndexDescriptor& index : desc.indexes) {
        SerializeIndexDescriptor(index, buffer);
    }
}

Status DeserializeTableDescriptor(const ByteBuffer& bytes, std::size_t* offset, TableDescriptor* out) {
    Status status = binary_io::ReadString(bytes, offset, &out->database_name);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->table_name);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->data_file);
    if (!status.ok()) {
        return status;
    }

    status = binary_io::ReadString(bytes, offset, &out->index_file);
    if (!status.ok()) {
        return status;
    }

    status = DeserializeTableSchema(bytes, offset, &out->schema);
    if (!status.ok()) {
        return status;
    }

    std::uint32_t index_count = 0;
    status = binary_io::ReadUint32(bytes, offset, &index_count);
    if (!status.ok()) {
        return status;
    }

    out->indexes.clear();
    out->indexes.reserve(index_count);
    for (std::uint32_t i = 0; i < index_count; ++i) {
        IndexDescriptor index;
        status = DeserializeIndexDescriptor(bytes, offset, &index);
        if (!status.ok()) {
            return status;
        }
        out->indexes.push_back(std::move(index));
    }

    return Status::Ok();
}

}
