#include "server/query_result.h"
#include "execution/value.h"

namespace db {

namespace {

// экранируем строку для json
std::string EscapeJson(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default: r += c; break;
        }
    }
    return r;
}

// преобразуем value в json
std::string ValueToJson(const Value& v) {
    if (v.is_null()) return "null"; // null без кавычек
    switch (v.type()) {
        case ValueType::kInt:    return std::to_string(v.AsInt()); // число
        case ValueType::kBool:   return v.AsBool() ? "true" : "false"; // булево
        case ValueType::kString: return std::string("\"") + EscapeJson(v.AsString()) + "\""; // строка
        default:                 return "null";
    }
}

} // anonymous namespace

// формируем json из queryresult
std::string QueryResultToJson(const QueryResult& result) {
    if (!result.ok) {
        return "{\"error\":\"" + EscapeJson(result.error) + "\"}"; // ошибка
    }
    if (result.rows.empty() && result.columns.empty()) {
        return "{\"affected_rows\":" + std::to_string(result.affected_rows) + "}"; // ddl/dml без строк
    }
    // select: массив объектов [{"col1":val1,...}, ...]
    std::string json = "[";
    for (size_t i = 0; i < result.rows.size(); ++i) {
        if (i > 0) json += ",";
        json += "{";
        const auto& row = result.rows[i].values;
        for (size_t j = 0; j < result.columns.size() && j < row.size(); ++j) {
            if (j > 0) json += ",";
            json += "\"" + EscapeJson(result.columns[j]) + "\":" + ValueToJson(row[j]);
        }
        json += "}";
    }
    json += "]";
    return json;
}

} // namespace db
