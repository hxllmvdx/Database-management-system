#pragma once
#include "../common/bytes.h"
#include "../common/status.h"
#include "../execution/value.h"

namespace db {

class KeyEncoder {
public:
    static Status Encode(const Value& value, ByteBuffer* out);
    static Status Decode(const ByteBuffer& bytes, ValueType type, Value* out);
};

}
