// исходный код написан человеком 1, комментарии и заготовки добавлены человеком 3
#include "server/query_processor.h" // публичный интерфейс обработчика
#include "server/database.h" // доступ к StorageNodeEngine (код человека 1)
#include "server/query_result.h" // формат результата
#include "common/value.h" // типы значений ячеек таблицы
#include <sstream> // строковые потоки
#include <algorithm> // std::transform для ToLower
#include <cctype> // std::tolower для обработки регистра

namespace db { // пространство имён базы данных

namespace { // внутренние хелперы и ЗАГОТОВКИ ДЛЯ ЧЕЛОВЕКА 2

// приводит строку к нижнему регистру (ключевые слова sql регистронезависимы)
std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), // обрабатываем каждый символ
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); // в нижний регистр
    return s; // возвращаем результат
} // ToLower

// ============================================================================
// ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2: парсинг sql → AST
// ============================================================================
// человек 2 ещё не писал свой код. когда реализует Parser — заменить на реальный вызов

struct ParseResult { // результат фазы парсинга, ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2
    bool ok = false; // true если синтаксис корректен
    std::string error; // описание синтаксической ошибки (строка, позиция)
    // TODO человека 2: добавить поле AST ast; — абстрактное синтаксическое дерево
}; // ParseResult

// заглушка парсера: человек 2 ещё не писал свой код, всегда возвращаем ошибку
ParseResult Parse(const std::string& sql) {
    ParseResult r; // формируем результат
    r.ok = false; // парсер не реализован (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    r.error = "parser not implemented (stub for person 2)"; // пояснение для клиента
    (void)sql; // подавляем warning о неиспользуемом параметре
    return r; // возвращаем заглушку
} // Parse — ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2

// ============================================================================
// ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2: планирование AST → PhysicalPlan
// ============================================================================
// человек 2 ещё не писал свой код. когда реализует Planner — заменить на реальный вызов

struct PlanResult { // результат фазы планирования, ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2
    bool ok = false; // true если план построен успешно
    std::string error; // описание семантической ошибки (таблица не найдена и т.д.)
    // TODO человека 2: добавить поле PhysicalPlan plan; — план выполнения
}; // PlanResult

// заглушка планировщика: человек 2 ещё не писал свой код
PlanResult Plan(const ParseResult& /*ast*/, SessionContext* /*session*/) {
    PlanResult r; // формируем результат
    r.ok = false; // планировщик не реализован (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    r.error = "planner not implemented (stub for person 2)"; // пояснение для клиента
    return r; // возвращаем заглушку
} // Plan — ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2

// ============================================================================
// ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2: выполнение PhysicalPlan → QueryResult
// ============================================================================
// человек 2 ещё не писал свой код. когда реализует Executor — заменить на реальный вызов

struct ExecuteResult { // результат фазы выполнения, ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2
    bool ok = false; // true если выполнение успешно
    std::string error; // описание runtime-ошибки (деление на ноль, deadlock и т.д.)
    QueryResult result; // данные результата (колонки, строки, affected_rows)
}; // ExecuteResult

// заглушка исполнителя: человек 2 ещё не писал свой код
ExecuteResult ExecutePlan(const PlanResult& /*plan*/, SessionContext* /*session*/) {
    ExecuteResult r; // формируем результат
    r.ok = false; // executor не реализован (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    r.error = "executor not implemented (stub for person 2)"; // пояснение для клиента
    return r; // возвращаем заглушку
} // ExecutePlan — ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2

} // anonymous namespace

QueryProcessor::QueryProcessor(Database* db) : db_(db) {} // сохраняем указатель на Database (код человека 1)

QueryResult QueryProcessor::Execute(const std::string& sql,
                                    SessionContext* session) {
    QueryResult result; // формируем результат выполнения
    result.ok = true; // по умолчанию оптимистично

    auto cmd = ToLower(sql); // приводим запрос к нижнему регистру для быстрой проверки

    // use database — обрабатываем локально, без parser/planner/execution (код человека 1)
    if (cmd.rfind("use ", 0) == 0) { // запрос начинается с "use "
        std::string db_name = sql.substr(4); // отрезаем префикс "use "
        size_t a = 0; // левая граница имени базы
        while (a < db_name.size() && std::isspace(static_cast<unsigned char>(db_name[a]))) ++a; // пропускаем пробелы слева
        size_t b = db_name.size(); // правая граница
        while (b > a && (std::isspace(static_cast<unsigned char>(db_name[b - 1])) || db_name[b - 1] == ';')) --b; // отрезаем пробелы и ; справа
        session->current_db = db_name.substr(a, b - a); // сохраняем текущую бд в контексте
        result.affected_rows = 0; // DDL-запрос не затрагивает строки
        return result; // возвращаем успех
    } // if USE

    // ========================================================================
    // pipeline обработки sql через три фазы (ЗАГОТОВКИ ДЛЯ ЧЕЛОВЕКА 2)
    // ========================================================================

    // фаза 1: парсинг sql → AST (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    ParseResult parse_res = Parse(sql); // вызов заглушки парсера (человек 2 ещё не писал код)
    if (!parse_res.ok) { // синтаксическая ошибка
        result.ok = false; // помечаем как ошибку
        result.error = "[parse] " + parse_res.error; // префикс фазы для отладки
        return result; // возвращаем ошибку клиенту
    } // if parse error

    // фаза 2: планирование AST → PhysicalPlan (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    PlanResult plan_res = Plan(parse_res, session); // вызов заглушки планировщика (человек 2 ещё не писал код)
    if (!plan_res.ok) { // семантическая ошибка (таблица не найдена, колонка отсутствует и т.д.)
        result.ok = false; // помечаем как ошибку
        result.error = "[plan] " + plan_res.error; // префикс фазы для отладки
        return result; // возвращаем ошибку клиенту
    } // if plan error

    // фаза 3: выполнение PhysicalPlan → QueryResult (ЗАГОТОВКА ДЛЯ ЧЕЛОВЕКА 2)
    ExecuteResult exec_res = ExecutePlan(plan_res, session); // вызов заглушки executor (человек 2 ещё не писал код)
    if (!exec_res.ok) { // runtime-ошибка (деление на ноль, deadlock, нарушение constraint)
        result.ok = false; // помечаем как ошибку
        result.error = "[execute] " + exec_res.error; // префикс фазы для отладки
        return result; // возвращаем ошибку клиенту
    } // if execute error

    return exec_res.result; // возвращаем результат от executor
} // Execute

} // namespace db
