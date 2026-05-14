// исходный код написан человеком 1, комментарии добавлены человеком 3
#include "server/query_result.h" // публичный интерфейс QueryResult и QueryResultToJson
#include "common/value.h"        // типы значений ячеек (int, string, bool, null)

namespace db { // пространство имён базы данных

namespace { // внутренние хелперы для преобразования в json

// экранирует спецсимволы внутри json-строки (кавычки, слеши, управляющие)
std::string EscapeJson(const std::string& s) {
    std::string r; // накопитель результата
    r.reserve(s.size()); // резервируем примерно столько же
    for (char c : s) { // идём по всем символам
        switch (c) { // выбираем замену
            case '"': r += "\\\""; break; // кавычка
            case '\\': r += "\\\\"; break; // обратный слеш
            case '\b': r += "\\b"; break; // забой
            case '\f': r += "\\f"; break; // перевод формата
            case '\n': r += "\\n"; break; // перевод строки
            case '\r': r += "\\r"; break; // возврат каретки
            case '\t': r += "\\t"; break; // табуляция
            default: r += c; break; // остальные как есть
        } // switch
    } // for
    return r; // экранированная строка
} // EscapeJson

// преобразует Value в json-представление (число, строка, bool, null)
std::string ValueToJson(const Value& v) {
    if (v.is_null()) return "null"; // null — литерал без кавычек
    switch (v.type()) { // в зависимости от типа
        case ValueType::kInt:    return std::to_string(v.AsInt()); // число → строка цифр
        case ValueType::kBool:   return v.AsBool() ? "true" : "false"; // булево как литерал
        case ValueType::kString: return std::string("\"") + EscapeJson(v.AsString()) + "\""; // строка в кавычках
        default:                 return "null"; // на всякий случай
    } // switch
} // ValueToJson

} // anonymous namespace

// преобразует QueryResult в json-строку для вывода в терминал клиента
std::string QueryResultToJson(const QueryResult& result) {
    if (!result.ok) { // запрос завершился с ошибкой
        return "{\"error\":\"" + EscapeJson(result.error) + "\"}"; // объект с полем error
    } // if error
    if (result.rows.empty() && result.columns.empty()) { // DDL или DML без возвращаемых строк
        return "{\"affected_rows\":" + std::to_string(result.affected_rows) + "}"; // только affected_rows
    } // if no rows
    // SELECT: массив объектов [{"col1":val1, "col2":val2}, ...]
    std::string json = "["; // начинаем массив
    for (size_t i = 0; i < result.rows.size(); ++i) { // идём по строкам
        if (i > 0) json += ","; // разделитель между объектами
        json += "{"; // начинаем объект строки
        const auto& row = result.rows[i].values; // значения текущей строки
        for (size_t j = 0; j < result.columns.size() && j < row.size(); ++j) { // идём по колонкам
            if (j > 0) json += ","; // разделитель между полями
            json += "\"" + EscapeJson(result.columns[j]) + "\":" + ValueToJson(row[j]); // "имя":значение
        } // for j
        json += "}"; // закрываем объект строки
    } // for i
    json += "]"; // закрываем массив
    return json; // возвращаем json-строку
} // QueryResultToJson

} // namespace db
