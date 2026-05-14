#include "common/value.h"

#include <stdexcept>
#include <utility>

db::Value::Value() : type_(ValueType::kNull), data_(std::monostate{}) {}

db::Value db::Value::Null() {
    return Value();
}

db::Value db::Value::Int(std::int64_t v) {
    Value value;
    value.type_ = ValueType::kInt;
    value.data_ = v;
    return value;
}

db::Value db::Value::String(std::string v) {
    Value value;
    value.type_ = ValueType::kString;
    value.data_ = std::move(v);
    return value;
}

db::Value db::Value::Bool(bool v) {
    Value value;
    value.type_ = ValueType::kBool;
    value.data_ = v;
    return value;
}

db::ValueType db::Value::type() const {
    return type_;
}

bool db::Value::is_null() const {
    return type_ == ValueType::kNull;
}

std::int64_t db::Value::AsInt() const {
    if (type_ != ValueType::kInt) {
        throw std::logic_error("Value is not an int");
    }
    return std::get<std::int64_t>(data_);
}

const std::string& db::Value::AsString() const {
    if (type_ != ValueType::kString) {
        throw std::logic_error("Value is not a string");
    }
    return std::get<std::string>(data_);
}

bool db::Value::AsBool() const {
    if (type_ != ValueType::kBool) {
        throw std::logic_error("Value is not a bool");
    }
    return std::get<bool>(data_);
}

bool db::Value::operator==(const Value& rhs) const {
    return type_ == rhs.type_ && data_ == rhs.data_;
}

bool db::Value::operator!=(const Value& rhs) const {
    return !(*this == rhs);
}

bool db::Value::operator<(const Value& rhs) const {
    if (type_ != rhs.type_) {
        return static_cast<int>(type_) < static_cast<int>(rhs.type_);
    }

    switch (type_) {
        case ValueType::kNull:
            return false;
        case ValueType::kInt:
            return AsInt() < rhs.AsInt();
        case ValueType::kString:
            return AsString() < rhs.AsString();
        case ValueType::kBool:
            return AsBool() < rhs.AsBool();
    }

    return false;
}

bool db::Value::operator<=(const Value& rhs) const {
    return *this < rhs || *this == rhs;
}

bool db::Value::operator>(const Value& rhs) const {
    return rhs < *this;
}

bool db::Value::operator>=(const Value& rhs) const {
    return !(*this < rhs);
}
