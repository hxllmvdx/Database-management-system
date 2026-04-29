#pragma once

#include <cstddef>

#include "../catalog/index_descriptor.h"
#include "../catalog/schema.h"
#include "../catalog/table_descriptor.h"
#include "../execution/value.h"
#include "bytes.h"
#include "status.h"

namespace db::catalog_serialization {

void SerializeValue(const Value& value, ByteBuffer* buffer);
Status DeserializeValue(const ByteBuffer& bytes, std::size_t* offset, Value* out);

void SerializeColumnSchema(const ColumnSchema& column, ByteBuffer* buffer);
Status DeserializeColumnSchema(const ByteBuffer& bytes, std::size_t* offset, ColumnSchema* out);

void SerializeTableSchema(const TableSchema& schema, ByteBuffer* buffer);
Status DeserializeTableSchema(const ByteBuffer& bytes, std::size_t* offset, TableSchema* out);

void SerializeIndexDescriptor(const IndexDescriptor& index, ByteBuffer* buffer);
Status DeserializeIndexDescriptor(const ByteBuffer& bytes, std::size_t* offset, IndexDescriptor* out);

void SerializeTableDescriptor(const TableDescriptor& desc, ByteBuffer* buffer);
Status DeserializeTableDescriptor(const ByteBuffer& bytes, std::size_t* offset, TableDescriptor* out);

}
