#include "execution/value.h"
#include "common/bytes.h"
#include "common/status.h"

namespace db::value_serialization {

void SerializeValue(const Value& value, ByteBuffer* buffer);
Status DeserializeValue(const ByteBuffer& bytes, std::size_t* offset, Value* out);

}
