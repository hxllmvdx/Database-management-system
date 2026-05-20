#pragma once

namespace db {

enum class IsolationLevel {
    kReadCommitted,
    kRepeatableRead,
    kSerializable,
};

}
