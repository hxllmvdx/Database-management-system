#pragma once
#include <string>
#include "../common/bytes.h"
#include "../execution/value.h"

namespace db {

class KeyEncoder {
public:
    static ByteBuffer Encode(const Value& value);
    static Value Decode(const ByteBuffer& bytes, ValueType type);
};

}
