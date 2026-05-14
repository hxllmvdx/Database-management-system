#include "server/query_processor.h"
#include "server/database.h"
#include "server/query_result.h"
#include "runtime/storage_node_engine.h"
#include "catalog/schema.h"
#include "execution/value.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace db {

namespace {

// приводим строку к нижнему регистру
std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// убираем пробелы с начала и конца
std::string Trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

// разбиваем строку по разделителю
std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> r;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = Trim(item);
        if (!item.empty()) r.push_back(item);
    }
    return r;
}

// разбиваем sql на токены по пробелам и скобкам/запятым
std::vector<std::string> Tokenize(const std::string& sql) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_string = false;
    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        if (c == '"') {
            if (in_string) {
                cur += c;
                tokens.push_back(cur);
                cur.clear();
                in_string = false;
            } else {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
                in_string = true;
                cur += c;
            }
            continue;
        }
        if (in_string) {
            cur += c;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == ',') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            if (c == '(' || c == ')' || c == ',') {
                tokens.emplace_back(1, c); // скобки и запятые тоже токены
            }
            continue;
        }
        if (c == ';') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            break;
        }
        cur += c;
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// парсим значение из строки
Value ParseValue(const std::string& s) {
    if (ToLower(s) == "null") return Value::Null();
    if (ToLower(s) == "true") return Value::Bool(true);
    if (ToLower(s) == "false") return Value::Bool(false);
    if (!s.empty() && s.front() == '"' && s.back() == '"') {
        return Value::String(s.substr(1, s.size() - 2)); // строка без кавычек
    }
    // пробуем как int
    try {
        size_t pos = 0;
        int64_t v = std::stoll(s, &pos);
        if (pos == s.size()) return Value::Int(v);
    } catch (...) {}
    return Value::Null();
}

// определяем тип колонки из строки
ColumnType ParseColumnType(const std::string& s) {
    auto low = ToLower(s);
    if (low == "string") return ColumnType::kString;
    if (low == "bool") return ColumnType::kBool;
    return ColumnType::kInt; // по умолчанию int
}

// разбираем полное имя таблицы (db.table или table)
std::pair<std::string, std::string> ParseTableName(const std::string& name, const std::string& current_db) {
    auto pos = name.find('.');
    if (pos != std::string::npos) {
        return {name.substr(0, pos), name.substr(pos + 1)};
    }
    return {current_db, name};
}

} // anonymous namespace

QueryProcessor::QueryProcessor(Database* db) : db_(db) {}

QueryResult QueryProcessor::Execute(const std::string& sql,
                                    SessionContext* session) {
    QueryResult result;
    result.ok = true;

    auto tokens = Tokenize(sql);
    if (tokens.empty()) {
        result.ok = false;
        result.error = "empty query";
        return result;
    }

    auto cmd = ToLower(tokens[0]);

    // use database
    if (cmd == "use" && tokens.size() >= 2) {
        session->current_db = tokens[1];
        result.affected_rows = 0;
        return result;
    }

    // create database
    if (cmd == "create" && tokens.size() >= 3 && ToLower(tokens[1]) == "database") {
        auto st = db_->engine().CreateDatabase(tokens[2]);
        if (!st.ok()) { result.ok = false; result.error = st.message(); }
        else { result.affected_rows = 0; }
        return result;
    }

    // drop database
    if (cmd == "drop" && tokens.size() >= 3 && ToLower(tokens[1]) == "database") {
        auto st = db_->engine().DropDatabase(tokens[2]);
        if (!st.ok()) { result.ok = false; result.error = st.message(); }
        else { result.affected_rows = 0; }
        return result;
    }

    // create table
    if (cmd == "create" && tokens.size() >= 4 && ToLower(tokens[1]) == "table") {
        std::string table_name = tokens[2];
        TableSchema schema;
        // токены после имени таблицы — описание колонок в скобках
        // простейший парсинг: ищем токены типа "colname", "type", "not_null", "indexed"
        for (size_t i = 3; i < tokens.size(); ++i) {
            auto low = ToLower(tokens[i]);
            if (low == "(" || low == ")" || low == ",") continue; // пропускаем разделители
            if (low == "not_null" || low == "not" || low == "null") continue;
            if (low == "indexed" || low == "default") continue;
            // ищем пару: имя колонки + тип
            if (i + 1 < tokens.size()) {
                // пропускаем, если следующий токен — разделитель (не тип)
                auto next_low = ToLower(tokens[i + 1]);
                if (next_low == "(" || next_low == ")" || next_low == ",") continue;
                ColumnSchema col;
                col.name = tokens[i];
                col.type = ParseColumnType(tokens[i + 1]);
                // проверяем модификаторы после типа
                size_t j = i + 2;
                while (j < tokens.size()) {
                    auto mod = ToLower(tokens[j]);
                    if (mod == "not_null") { col.not_null = true; ++j; }
                    else if (mod == "indexed") { col.indexed = true; ++j; }
                    else break;
                }
                schema.columns.push_back(std::move(col));
                i = j - 1;
            }
        }

        // получаем catalog manager для создания таблицы
        auto [db_name, tbl] = ParseTableName(table_name, session->current_db);
        if (db_name.empty()) { result.ok = false; result.error = "no database selected"; return result; }

        TableDescriptor desc;
        desc.database_name = db_name;
        desc.table_name = tbl;
        desc.schema = std::move(schema);

        auto st = db_->engine().CreateTable(desc);
        if (!st.ok()) { result.ok = false; result.error = st.message(); }
        else { result.affected_rows = 0; }
        return result;
    }

    // drop table
    if (cmd == "drop" && tokens.size() >= 3 && ToLower(tokens[1]) == "table") {
        auto [db_name, tbl] = ParseTableName(tokens[2], session->current_db);
        if (db_name.empty()) { result.ok = false; result.error = "no database selected"; return result; }
        auto st = db_->engine().DropTable(db_name, tbl);
        if (!st.ok()) { result.ok = false; result.error = st.message(); }
        else { result.affected_rows = 0; }
        return result;
    }

    // insert into
    if (cmd == "insert" && tokens.size() >= 6 && ToLower(tokens[1]) == "into") {
        std::string table_name = tokens[2];
        auto [db_name, tbl] = ParseTableName(table_name, session->current_db);
        if (db_name.empty()) { result.ok = false; result.error = "no database selected"; return result; }

        // получаем дескриптор таблицы
        TableDescriptor desc;
        auto st = db_->engine().GetTableDescriptor(db_name, tbl, &desc);
        if (!st.ok()) { result.ok = false; result.error = st.message(); return result; }

        // ищем список колонок: между ( и )
        std::vector<std::string> col_names;
        size_t i = 3;
        // пропускаем токен до открывающей скобки
        while (i < tokens.size() && tokens[i] != "(") ++i;
        ++i; // пропускаем (
        while (i < tokens.size() && tokens[i] != ")") {
            col_names.push_back(tokens[i]);
            ++i;
        }
        if (i >= tokens.size()) { result.ok = false; result.error = "missing columns"; return result; }
        ++i; // пропускаем )

        // ищем VALUES
        while (i < tokens.size() && ToLower(tokens[i]) != "values") ++i;
        if (i >= tokens.size()) { result.ok = false; result.error = "missing VALUES"; return result; }
        ++i; // пропускаем VALUES

        // парсим значения между ( и )
        while (i < tokens.size() && tokens[i] != "(") ++i;
        ++i;
        std::vector<Value> values;
        while (i < tokens.size() && tokens[i] != ")") {
            values.push_back(ParseValue(tokens[i]));
            ++i;
        }

        // формируем tuple по схеме
        Tuple tuple;
        tuple.values.resize(desc.schema.columns.size(), Value::Null());
        for (size_t c = 0; c < col_names.size() && c < values.size(); ++c) {
            int idx = desc.schema.FindColumn(col_names[c]);
            if (idx >= 0) tuple.values[idx] = values[c];
        }

        RowId rid{};
        st = db_->engine().Insert(db_name, tbl, tuple, &rid);
        if (!st.ok()) { result.ok = false; result.error = st.message(); }
        else { result.affected_rows = 1; }
        return result;
    }

    // select
    if (cmd == "select" && tokens.size() >= 4) {
        // ищем FROM
        size_t from_idx = 1;
        while (from_idx < tokens.size() && ToLower(tokens[from_idx]) != "from") ++from_idx;
        if (from_idx >= tokens.size() - 1) {
            result.ok = false;
            result.error = "missing FROM";
            return result;
        }
        std::string table_name = tokens[from_idx + 1];
        auto [db_name, tbl] = ParseTableName(table_name, session->current_db);
        if (db_name.empty()) { result.ok = false; result.error = "no database selected"; return result; }

        // получаем дескриптор для списка колонок
        TableDescriptor desc;
        auto st = db_->engine().GetTableDescriptor(db_name, tbl, &desc);
        if (!st.ok()) { result.ok = false; result.error = st.message(); return result; }

        // сканируем таблицу
        std::vector<Row> rows;
        st = db_->engine().ScanTable(db_name, tbl, &rows);
        if (!st.ok()) { result.ok = false; result.error = st.message(); return result; }

        // формируем result
        for (const auto& col : desc.schema.columns) {
            result.columns.push_back(col.name);
        }
        for (const auto& row : rows) {
            if (row.deleted) continue; // пропускаем удалённые
            result.rows.push_back(row.tuple);
        }
        result.affected_rows = result.rows.size();
        return result;
    }

    // delete from
    if (cmd == "delete" && tokens.size() >= 3 && ToLower(tokens[1]) == "from") {
        std::string table_name = tokens[2];
        auto [db_name, tbl] = ParseTableName(table_name, session->current_db);
        if (db_name.empty()) { result.ok = false; result.error = "no database selected"; return result; }

        std::vector<Row> rows;
        auto st = db_->engine().ScanTable(db_name, tbl, &rows);
        if (!st.ok()) { result.ok = false; result.error = st.message(); return result; }

        size_t deleted = 0;
        for (const auto& row : rows) {
            if (row.deleted) continue;
            st = db_->engine().Delete(db_name, tbl, row.rid);
            if (st.ok()) ++deleted;
        }
        result.affected_rows = deleted;
        return result;
    }

    result.ok = false;
    result.error = "unsupported query (mvp): " + sql;
    return result;
}

} // namespace db
