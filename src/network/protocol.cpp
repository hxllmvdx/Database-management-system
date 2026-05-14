#include "network/protocol.h" // публичный интерфейс сериализации
#include "network/request.h"  // структура запроса
#include "network/response.h" // структура ответа
#include "server/query_result.h" // результат для сериализации rows
#include "common/value.h" // типы значений в ячейках таблицы
#include <cstdint> // фиксированные целочисленные типы (uint32_t)
#include <sstream> // потоки для сборки строк
#include <vector>  // динамический массив

namespace db { // пространство имён базы данных

namespace { // внутренние хелперы, не экспортируются наружу

// экранирует управляющие символы внутри json-строки
std::string EscapeJson(const std::string& s) {
    std::string r; // накопитель результата
    r.reserve(s.size() + 2); // резервируем немного больше для типичного случая
    for (char c : s) { // идём по всем символам входной строки
        switch (c) { // выбираем замену в зависимости от символа
            case '"': r += "\\\""; break; // кавычка → \"
            case '\\': r += "\\\\"; break; // обратный слеш → \\
            case '\b': r += "\\b"; break; // забой
            case '\f': r += "\\f"; break; // перевод формата
            case '\n': r += "\\n"; break; // перевод строки
            case '\r': r += "\\r"; break; // возврат каретки
            case '\t': r += "\\t"; break; // табуляция
            default: r += c; break; // остальные символы без изменений
        } // switch
    } // for
    return r; // возвращаем экранированную строку
} // EscapeJson

// превращает Value (int/string/bool/null) в json-представление
std::string ValueToJson(const Value& v) {
    if (v.is_null()) return "null"; // null передаётся без кавычек как литерал
    switch (v.type()) { // выбор в зависимости от типа значения
        case ValueType::kInt:    return std::to_string(v.AsInt()); // число → строка цифр
        case ValueType::kBool:   return v.AsBool() ? "true" : "false"; // булево как литерал
        case ValueType::kString: return std::string("\"") + EscapeJson(v.AsString()) + "\""; // строка в кавычках
        default:                 return "null"; // на всякий случай — null
    } // switch
} // ValueToJson

// минимальный рекурсивный парсер json для нужного подмножества (строки, числа, массивы)
struct JsonParser {
    const std::string& s; // ссылка на исходную строку json
    size_t pos = 0;       // текущая позиция чтения

    // пропускает пробелы, табы и переносы строк
    void skip_spaces() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) ++pos;
    } // skip_spaces

    // пытается съесть ожидаемый символ, возвращает true если получилось
    bool match(char c) {
        skip_spaces(); // сначала пропускаем пробелы
        if (pos < s.size() && s[pos] == c) { ++pos; return true; } // символ найден и пропущен
        return false; // символ не тот или конец строки
    } // match

    // читает строку в двойных кавычках с поддержкой escape-последовательностей
    std::string parse_string() {
        skip_spaces(); // пропускаем ведущие пробелы
        if (pos >= s.size() || s[pos] != '"') return ""; // не строка — возвращаем пусто
        ++pos; // пропускаем открывающую кавычку
        std::string r; // накопитель результата
        while (pos < s.size() && s[pos] != '"') { // пока не закрывающая кавычка
            if (s[pos] == '\\' && pos + 1 < s.size()) { // escape-последовательность
                char n = s[pos + 1]; // символ после бэкслеша
                if (n == '"') { r += '"'; pos += 2; } // экранированная кавычка
                else if (n == '\\') { r += '\\'; pos += 2; } // экранированный слеш
                else if (n == 'b') { r += '\b'; pos += 2; } // забой
                else if (n == 'f') { r += '\f'; pos += 2; } // перевод формата
                else if (n == 'n') { r += '\n'; pos += 2; } // перевод строки
                else if (n == 'r') { r += '\r'; pos += 2; } // возврат каретки
                else if (n == 't') { r += '\t'; pos += 2; } // табуляция
                else { r += s[pos]; ++pos; } // неизвестный escape — копируем как есть
            } else { // обычный символ
                r += s[pos++]; // копируем и двигаем позицию
            } // if escape
        } // while
        if (pos < s.size() && s[pos] == '"') ++pos; // пропускаем закрывающую кавычку
        return r; // возвращаем разэкранированную строку
    } // parse_string

    // читает json-value: строка, число, true, false или null
    Value parse_value() {
        skip_spaces(); // пропускаем пробелы
        if (pos >= s.size()) return Value::Null(); // конец строки — считаем null
        if (s[pos] == '"') return Value::String(parse_string()); // строковый литерал
        if (s[pos] == 't') { // возможно true
            if (s.compare(pos, 4, "true") == 0) { pos += 4; return Value::Bool(true); } // булево true
        } // if 't'
        if (s[pos] == 'f') { // возможно false
            if (s.compare(pos, 5, "false") == 0) { pos += 5; return Value::Bool(false); } // булево false
        } // if 'f'
        if (s[pos] == 'n') { // возможно null
            if (s.compare(pos, 4, "null") == 0) { pos += 4; return Value::Null(); } // литерал null
        } // if 'n'
        // число (целое со знаком)
        if (s[pos] == '-' || (s[pos] >= '0' && s[pos] <= '9')) { // начинается с цифры или минуса
            size_t end = pos; // конец числа
            if (s[end] == '-') ++end; // пропускаем знак
            while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end; // все цифры
            try { // пробуем преобразовать
                int64_t val = std::stoll(s.substr(pos, end - pos)); // строка → int64
                pos = end; // сдвигаем позицию за число
                return Value::Int(val); // упаковываем в Value
            } catch (...) { // не удалось распарсить — считаем null
                return Value::Null();
            } // try/catch
        } // if number
        return Value::Null(); // не распознали — возвращаем null
    } // parse_value

    // читает json-массив значений [...] и возвращает вектор Value (одна строка таблицы)
    std::vector<Value> parse_value_array() {
        std::vector<Value> r; // накопитель
        skip_spaces(); // пропускаем пробелы
        if (!match('[')) return r; // не массив — пустой результат
        skip_spaces(); // пробелы после [
        if (match(']')) return r; // пустой массив []
        while (true) { // читаем элементы
            r.push_back(parse_value()); // следующее значение
            skip_spaces(); // пробелы после значения
            if (match(',')) continue; // ещё элементы
            if (match(']')) break; // конец массива
            break; // некорректный формат — выходим
        } // while
        return r; // возвращаем строку как вектор Value
    } // parse_value_array

    // читает json-массив строк [...] для колонок
    std::vector<std::string> parse_string_array() {
        std::vector<std::string> r; // накопитель
        skip_spaces(); // пропускаем пробелы
        if (!match('[')) return r; // не массив
        skip_spaces(); // пробелы после [
        if (match(']')) return r; // пустой массив
        while (true) { // читаем строки
            r.push_back(parse_string()); // следующая строка
            skip_spaces(); // пробелы
            if (match(',')) continue; // ещё элементы
            if (match(']')) break; // конец массива
            break; // некорректный формат
        } // while
        return r; // список имён колонок
    } // parse_string_array

    // читает json-массив массивов [[...], [...]] — все строки таблицы
    std::vector<Tuple> parse_rows_array() {
        std::vector<Tuple> r; // накопитель строк
        skip_spaces(); // пропускаем пробелы
        if (!match('[')) return r; // не массив
        skip_spaces(); // пробелы после [
        if (match(']')) return r; // пустой массив
        while (true) { // читаем строки
            Tuple t; // одна строка
            t.values = parse_value_array(); // парсим внутренний массив
            r.push_back(std::move(t)); // сохраняем строку
            skip_spaces(); // пробелы
            if (match(',')) continue; // ещё строки
            if (match(']')) break; // конец внешнего массива
            break; // некорректный формат
        } // while
        return r; // все строки результата
    } // parse_rows_array
}; // JsonParser

} // anonymous namespace

// сериализует Request в length-prefixed json: 4 байта длины + payload
std::string Protocol::SerializeRequest(const Request& req) {
    std::string json = "{\"id\":\"" + EscapeJson(req.request_id) + // начинаем json-объект
                       "\",\"db\":\"" + EscapeJson(req.database) + // поле базы данных
                       "\",\"sql\":\"" + EscapeJson(req.sql) + // поле sql-запроса
                       "\",\"auth\":\"" + EscapeJson(req.auth_token) + "\"}"; // поле токена
    uint32_t len = static_cast<uint32_t>(json.size()); // длина payload
    std::string result; // итоговая бинарная строка
    result.reserve(4 + json.size()); // резервируем память сразу
    result.append(reinterpret_cast<const char*>(&len), 4); // записываем длину в little-endian
    result += json; // добавляем сам json
    return result; // возвращаем wire-формат
} // SerializeRequest

// десериализует Request из length-prefixed json, true если успешно
bool Protocol::DeserializeRequest(const std::string& data, Request* out) {
    if (data.size() < 4) return false; // минимум 4 байта на длину
    uint32_t len = 0; // читаем длину
    std::memcpy(&len, data.data(), 4); // копируем 4 байта в uint32
    if (data.size() < 4 + len) return false; // данных недостаточно
    std::string json = data.substr(4, len); // вырезаем json-пayload

    JsonParser p{json, 0}; // создаём парсер
    if (!p.match('{')) return false; // ожидаем объект
    while (true) { // читаем пары ключ:значение
        p.skip_spaces(); // пропускаем пробелы
        if (p.match('}')) break; // конец объекта
        std::string key = p.parse_string(); // ключ
        if (!p.match(':')) return false; // ожидаем двоеточие
        if (key == "id")       out->request_id = p.parse_string(); // идентификатор запроса
        else if (key == "db")  out->database = p.parse_string(); // имя базы данных
        else if (key == "sql") out->sql = p.parse_string(); // текст sql
        else if (key == "auth")out->auth_token = p.parse_string(); // токен
        else { p.parse_string(); } // пропускаем неизвестные ключи для совместимости
        p.skip_spaces(); // пробелы после значения
        if (p.match(',')) continue; // ещё поля
        if (p.match('}')) break; // конец объекта
        return false; // неожиданный символ — ошибка формата
    } // while
    return true; // успешно распарсили
} // DeserializeRequest

// сериализует Response с QueryResult в length-prefixed json
std::string Protocol::SerializeResponse(const Response& resp) {
    std::string json = "{\"ok\":" + std::string(resp.ok ? "true" : "false") + // флаг успеха
                       ",\"error\":\"" + EscapeJson(resp.error) + "\""; // текст ошибки
    // сериализуем массив имён колонок
    json += ",\"columns\":["; // начинаем массив
    for (size_t i = 0; i < resp.result.columns.size(); ++i) { // идём по колонкам
        if (i > 0) json += ","; // разделитель между элементами
        json += "\"" + EscapeJson(resp.result.columns[i]) + "\""; // строка в кавычках
    } // for
    json += "]"; // закрываем массив колонок
    // сериализуем массив строк (rows)
    json += ",\"rows\":["; // начинаем массив строк
    for (size_t i = 0; i < resp.result.rows.size(); ++i) { // идём по строкам
        if (i > 0) json += ","; // разделитель между строками
        json += "["; // начинаем внутренний массив значений
        const auto& vals = resp.result.rows[i].values; // значения текущей строки
        for (size_t j = 0; j < vals.size(); ++j) { // идём по колонкам строки
            if (j > 0) json += ","; // разделитель между значениями
            json += ValueToJson(vals[j]); // сериализуем Value
        } // for j
        json += "]"; // закрываем внутренний массив
    } // for i
    json += "]"; // закрываем массив строк
    // сериализуем affected_rows
    json += ",\"affected\":" + std::to_string(resp.result.affected_rows) + "}"; // число и закрываем объект

    uint32_t len = static_cast<uint32_t>(json.size()); // длина payload
    std::string result; // итоговая бинарная строка
    result.reserve(4 + json.size()); // резервируем память
    result.append(reinterpret_cast<const char*>(&len), 4); // 4 байта длины little-endian
    result += json; // добавляем json
    return result; // wire-формат ответа
} // SerializeResponse

// десериализует Response из length-prefixed json, true если успешно
bool Protocol::DeserializeResponse(const std::string& data, Response* out) {
    if (data.size() < 4) return false; // минимум 4 байта
    uint32_t len = 0; // длина payload
    std::memcpy(&len, data.data(), 4); // читаем длину
    if (data.size() < 4 + len) return false; // проверяем достаточность данных
    std::string json = data.substr(4, len); // вырезаем json

    JsonParser p{json, 0}; // создаём парсер
    if (!p.match('{')) return false; // ожидаем объект
    while (true) { // читаем поля
        p.skip_spaces(); // пропускаем пробелы
        if (p.match('}')) break; // конец объекта
        std::string key = p.parse_string(); // ключ
        if (!p.match(':')) return false; // ожидаем двоеточие
        if (key == "ok") { // флаг успеха
            p.skip_spaces(); // пропускаем пробелы
            if (p.s.compare(p.pos, 4, "true") == 0) { out->ok = true; p.pos += 4; } // true
            else if (p.s.compare(p.pos, 5, "false") == 0) { out->ok = false; p.pos += 5; } // false
        } else if (key == "error") out->error = p.parse_string(); // текст ошибки
        else if (key == "columns") out->result.columns = p.parse_string_array(); // имена колонок
        else if (key == "rows") out->result.rows = p.parse_rows_array(); // строки результата
        else if (key == "affected") { // количество затронутых строк
            Value v = p.parse_value(); // читаем число
            if (v.type() == ValueType::kInt) out->result.affected_rows = static_cast<size_t>(v.AsInt()); // сохраняем
        } else { p.parse_value(); } // пропускаем неизвестные поля
        p.skip_spaces(); // пробелы после значения
        if (p.match(',')) continue; // ещё поля
        if (p.match('}')) break; // конец объекта
        return false; // ошибка формата
    } // while
    return true; // успешно
} // DeserializeResponse

} // namespace db
