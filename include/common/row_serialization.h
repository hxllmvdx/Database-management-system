#pragma once

#include "common/status.h"
#include "execution/tuple.h"
#include "common/bytes.h"
#include "storage/row.h"

namespace db::row_serialization {

void SerializeTuple(const Tuple& tuple, ByteBuffer* buffer);
Status DeserializeTuple(const ByteBuffer& bytes, std::size_t* offset, Tuple* tuple);

void SerializeRow(const Row& row, ByteBuffer* buffer);
Status DeserializeRow(const ByteBuffer& bytes, std::size_t* offset, Row* row);

}
