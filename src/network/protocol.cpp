#include "network/protocol.h"
#include "network/request.h"
#include "network/response.h"
#include "server/query_result.h"
#include "execution/value.h"
#include <cstdint>
#include <sstream>
#include <vector>

namespace db {

namespace { // внутренние хелперы, не видны снаружи

// экранирует строку для json: кавычки, слеши и управляющие символы
std::string EscapeJson(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
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

// превращает value в json-представление
std::string ValueToJson(const Value& v) {
    if (v.is_null()) return "null"; // null без кавычек
    switch (v.type()) {
        case ValueType::kInt:    return std::to_string(v.AsInt()); // число
        case ValueType::kBool:   return v.AsBool() ? "true" : "false"; // булево
        case ValueType::kString: return std::string("\"") + EscapeJson(v.AsString()) + "\""; // строка в кавычках
        default:                 return "null";
    }
}

// минимальный json-парсер для нужного нам подмножества
struct JsonParser {
    const std::string& s; // исходная строка
    size_t pos = 0; // текущая позиция

    void skip_spaces() { // пропускаем пробелы
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) ++pos;
    }

    bool match(char c) { // проверяем и пропускаем один символ
        skip_spaces();
        if (pos < s.size() && s[pos] == c) { ++pos; return true; }
        return false;
    }

    // читаем строку в двойных кавычках
    std::string parse_string() {
        skip_spaces();
        if (pos >= s.size() || s[pos] != '"') return "";
        ++pos; // пропускаем открывающую кавычку
        std::string r;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\' && pos + 1 < s.size()) {
                char n = s[pos + 1];
                if (n == '"') { r += '"'; pos += 2; }
                else if (n == '\\') { r += '\\'; pos += 2; }
                else if (n == 'b') { r += '\b'; pos += 2; }
                else if (n == 'f') { r += '\f'; pos += 2; }
                else if (n == 'n') { r += '\n'; pos += 2; }
                else if (n == 'r') { r += '\r'; pos += 2; }
                else if (n == 't') { r += '\t'; pos += 2; }
                else { r += s[pos]; ++pos; }
            } else {
                r += s[pos++];
            }
        }
        if (pos < s.size() && s[pos] == '"') ++pos; // пропускаем закрывающую кавычку
        return r;
    }

    // читаем значение: строка, число, true, false, null
    Value parse_value() {
        skip_spaces();
        if (pos >= s.size()) return Value::Null();
        if (s[pos] == '"') return Value::String(parse_string()); // строка
        if (s[pos] == 't') { // true
            if (s.compare(pos, 4, "true") == 0) { pos += 4; return Value::Bool(true); }
        }
        if (s[pos] == 'f') { // false
            if (s.compare(pos, 5, "false") == 0) { pos += 5; return Value::Bool(false); }
        }
        if (s[pos] == 'n') { // null
            if (s.compare(pos, 4, "null") == 0) { pos += 4; return Value::Null(); }
        }
        // число (целое, возможно со знаком)
        if (s[pos] == '-' || (s[pos] >= '0' && s[pos] <= '9')) {
            size_t end = pos;
            if (s[end] == '-') ++end;
            while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end;
            try {
                int64_t val = std::stoll(s.substr(pos, end - pos));
                pos = end;
                return Value::Int(val);
            } catch (...) {
                return Value::Null();
            }
        }
        return Value::Null();
    }

    // читаем массив значений (tuple)
    std::vector<Value> parse_value_array() {
        std::vector<Value> r;
        skip_spaces();
        if (!match('[')) return r;
        skip_spaces();
        if (match(']')) return r; // пустой массив
        while (true) {
            r.push_back(parse_value());
            skip_spaces();
            if (match(',')) continue;
            if (match(']')) break;
            // некорректный формат — выходим
            break;
        }
        return r;
    }

    // читаем массив строк
    std::vector<std::string> parse_string_array() {
        std::vector<std::string> r;
        skip_spaces();
        if (!match('[')) return r;
        skip_spaces();
        if (match(']')) return r; // пустой массив
        while (true) {
            r.push_back(parse_string());
            skip_spaces();
            if (match(',')) continue;
            if (match(']')) break;
            break;
        }
        return r;
    }

    // читаем массив массивов значений (rows)
    std::vector<Tuple> parse_rows_array() {
        std::vector<Tuple> r;
        skip_spaces();
        if (!match('[')) return r;
        skip_spaces();
        if (match(']')) return r; // пустой массив
        while (true) {
            Tuple t;
            t.values = parse_value_array();
            r.push_back(std::move(t));
            skip_spaces();
            if (match(',')) continue;
            if (match(']')) break;
            break;
        }
        return r;
    }
};

} // anonymous namespace

// сериализуем request: 4 байта длины (little-endian) + json
std::string Protocol::SerializeRequest(const Request& req) {
    std::string json = "{\"id\":\"" + EscapeJson(req.request_id) +
                       "\",\"db\":\"" + EscapeJson(req.database) +
                       "\",\"sql\":\"" + EscapeJson(req.sql) +
                       "\",\"auth\":\"" + EscapeJson(req.auth_token) + "\"}";
    uint32_t len = static_cast<uint32_t>(json.size());
    std::string result;
    result.reserve(4 + json.size());
    result.append(reinterpret_cast<const char*>(&len), 4); // записываем длину
    result += json; // добавляем сам json
    return result;
}

// десериализуем request из length-prefixed json
bool Protocol::DeserializeRequest(const std::string& data, Request* out) {
    if (data.size() < 4) return false; // минимум 4 байта на длину
    uint32_t len = 0;
    std::memcpy(&len, data.data(), 4); // читаем длину
    if (data.size() < 4 + len) return false; // данных недостаточно
    std::string json = data.substr(4, len); // вырезаем json

    JsonParser p{json, 0};
    if (!p.match('{')) return false; // должен начинаться с {
    while (true) {
        p.skip_spaces();
        if (p.match('}')) break; // конец объекта
        std::string key = p.parse_string(); // читаем ключ
        if (!p.match(':')) return false; // ожидаем двоеточие
        if (key == "id")       out->request_id = p.parse_string();
        else if (key == "db")  out->database = p.parse_string();
        else if (key == "sql") out->sql = p.parse_string();
        else if (key == "auth")out->auth_token = p.parse_string();
        else { p.parse_string(); } // пропускаем неизвестные ключи
        p.skip_spaces();
        if (p.match(',')) continue;
        if (p.match('}')) break;
        return false;
    }
    return true;
}

// сериализуем response: 4 байта длины + json с queryresult
std::string Protocol::SerializeResponse(const Response& resp) {
    std::string json = "{\"ok\":" + std::string(resp.ok ? "true" : "false") +
                       ",\"error\":\"" + EscapeJson(resp.error) + "\"";
    // columns
    json += ",\"columns\":[";
    for (size_t i = 0; i < resp.result.columns.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + EscapeJson(resp.result.columns[i]) + "\"";
    }
    json += "]";
    // rows
    json += ",\"rows\":[";
    for (size_t i = 0; i < resp.result.rows.size(); ++i) {
        if (i > 0) json += ",";
        json += "[";
        const auto& vals = resp.result.rows[i].values;
        for (size_t j = 0; j < vals.size(); ++j) {
            if (j > 0) json += ",";
            json += ValueToJson(vals[j]);
        }
        json += "]";
    }
    json += "]";
    // affected
    json += ",\"affected\":" + std::to_string(resp.result.affected_rows) + "}";

    uint32_t len = static_cast<uint32_t>(json.size());
    std::string result;
    result.reserve(4 + json.size());
    result.append(reinterpret_cast<const char*>(&len), 4);
    result += json;
    return result;
}

// десериализуем response из length-prefixed json
bool Protocol::DeserializeResponse(const std::string& data, Response* out) {
    if (data.size() < 4) return false;
    uint32_t len = 0;
    std::memcpy(&len, data.data(), 4);
    if (data.size() < 4 + len) return false;
    std::string json = data.substr(4, len);

    JsonParser p{json, 0};
    if (!p.match('{')) return false;
    while (true) {
        p.skip_spaces();
        if (p.match('}')) break;
        std::string key = p.parse_string();
        if (!p.match(':')) return false;
        if (key == "ok") {
            p.skip_spaces();
            if (p.s.compare(p.pos, 4, "true") == 0) { out->ok = true; p.pos += 4; }
            else if (p.s.compare(p.pos, 5, "false") == 0) { out->ok = false; p.pos += 5; }
        }
        else if (key == "error") out->error = p.parse_string();
        else if (key == "columns") out->result.columns = p.parse_string_array();
        else if (key == "rows") out->result.rows = p.parse_rows_array();
        else if (key == "affected") {
            Value v = p.parse_value();
            if (v.type() == ValueType::kInt) out->result.affected_rows = static_cast<size_t>(v.AsInt());
        }
        else { p.parse_value(); } // пропускаем неизвестное
        p.skip_spaces();
        if (p.match(',')) continue;
        if (p.match('}')) break;
        return false;
    }
    return true;
}

} // namespace db
