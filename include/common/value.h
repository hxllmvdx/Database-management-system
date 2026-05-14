#pragma once
#include <cstdint>
#include <string>
#include <variant>

namespace db {

enum class ValueType {
    kNull,
    kInt,
    kString,
    kBool,
};

class Value {
public:
    Value();
    static Value Null();
    static Value Int(std::int64_t v);
    static Value String(std::string v);
    static Value Bool(bool v);

    ValueType type() const;
    bool is_null() const;

    std::int64_t AsInt() const;
    const std::string& AsString() const;
    bool AsBool() const;

    bool operator==(const Value& rhs) const;
    bool operator!=(const Value& rhs) const;
    bool operator<(const Value& rhs) const;
    bool operator<=(const Value& rhs) const;
    bool operator>(const Value& rhs) const;
    bool operator>=(const Value& rhs) const;

private:
    ValueType type_;
    std::variant<std::monostate, std::int64_t, std::string, bool> data_;
};

}
