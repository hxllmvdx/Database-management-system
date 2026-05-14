# План: Зона ответственности Человека 3

## Архитектура целевого pipeline

```
CLI → TcpClient → TcpServer → StorageService → QueryProcessor
                                                    ↓
                                            [Parser → Planner → Executor]
                                                    ↓
                                          StorageNodeEngine (человек 1)
                                                    ↓
                                               Response
```

## Этап 1. Укрепление контрактов

Цель: сделать так, чтобы `Request / Response / QueryResult / Session / SessionContext` были стабильной границей между всеми тремя участниками.

- [ ] Задокументировать поля каждой структуры (что означает, кто заполняет, кто читает).
- [ ] Убедиться, что `QueryResult` не тянет за собой внутренние типы чужих модулей. Сейчас он использует `Tuple`/`Value` из `common/` — нужно зафиксировать это как часть публичного контракта.
- [ ] Определить единый формат ошибок: `Response.ok + Response.error` vs исключения vs `Status`. В pipeline ошибка должна превращаться в `Response` одинаково для всех слоёв.

## Этап 2. Protocol ✅

- [x] `SerializeRequest / DeserializeRequest`
- [x] `SerializeResponse / DeserializeResponse`
- [x] Round-trip тесты (`tests/network/protocol_tests.cpp`)

## Этап 3. TCP Server / Client ✅

- [x] `TcpServer` принимает соединения и запросы.
- [x] `TcpClient` отправляет запрос и читает ответ.
- [x] Сетевые ошибки мапятся в `Status` (не в исключения).
- [x] Smoke tests: round-trip и несколько последовательных запросов в одном соединении.

## Этап 4. Session + SessionContext ✅

- [x] `Session`: transport/session metadata (`client_id` и т.д.).
- [x] `SessionContext`: текущая БД, пользователь, auth state. Живёт внутри `StorageService` и передаётся в `QueryProcessor`.

## Этап 5. StorageService ✅

- [x] Принимает `Request + Session`.
- [x] Создаёт / обновляет `SessionContext` по `client_id`.
- [x] Обрабатывает `USE database` и `auth_token`.
- [x] Вызывает `QueryProcessor::Execute`.
- [x] Упаковывает `QueryResult` в `Response`.

## Этап 6. Database — абстракция над StorageNodeEngine

Цель: `Database` держит `StorageNodeEngine`, но не ломает компиляцию, если хедера человека 1 ещё нет.

- [ ] Определить чёткий lifecycle `Start() / Stop()` без прямой зависимости от хедера `StorageNodeEngine`. Сейчас используется forward declaration + `StorageNodeEngine*` — нужно либо оставить так, либо ввести свой интерфейс-фасад.
- [ ] Тест: `Database` стартует и останавливается без падений (`tests/integration/database_tests.cpp`).
- [ ] Тест: `Config` прокидывается корректно.

**Синхронизация:** с человеком 1 — API `StorageNodeEngine`, startup/shutdown sequence, коды ошибок low-level.

## Этап 7. QueryProcessor — заготовки под человека 2

Цель: подготовить скелет `QueryProcessor::Execute`, в который человек 2 позже вставит свои модули. Сейчас там только обработка `USE database`. Нужно разбить метод на фазы и оставить чёткие точки входа.

### 7.1. Структура Execute() (заготовки)

Разбить `Execute(sql, session)` на три фазы с чёткими контрактами:

```cpp
// Phase 1: Parse
// Заглушка. Человек 2 реализует Parser::Parse(sql) → AST или ParseError.
// Контракт: вход — std::string, выход — либо AST-структура, либо ошибка с позицией.
auto parse_result = Parse(sql);
if (!parse_result.ok) return QueryResult::Error(StatusCode::kParseError, parse_result.error);

// Phase 2: Plan
// Заглушка. Человек 2 реализует Planner::Plan(ast, session) → PhysicalPlan или PlanError.
// Контракт: вход — AST + SessionContext, выход — PhysicalPlan (или ошибка семантики/биндинга).
auto plan_result = Plan(parse_result.ast, session);
if (!plan_result.ok) return QueryResult::Error(StatusCode::kPlanError, plan_result.error);

// Phase 3: Execute
// Заглушка. Человек 2 реализует Executor::Run(plan, session) → QueryResult или ExecutionError.
// Контракт: вход — PhysicalPlan + SessionContext, выход — QueryResult (колонки, строки, affected_rows).
auto exec_result = ExecutePlan(plan_result.plan, session);
if (!exec_result.ok) return QueryResult::Error(StatusCode::kExecutionError, exec_result.error);
return exec_result.result;
```

Что сделать сейчас:
- [ ] Определить `struct ParseResult { bool ok; std::string error; /* AST stub */ };`
- [ ] Определить `struct PlanResult { bool ok; std::string error; /* PhysicalPlan stub */ };`
- [ ] Определить `struct ExecuteResult { bool ok; std::string error; QueryResult result; };`
- [ ] Создать функции-заглушки `Parse()`, `Plan()`, `ExecutePlan()` внутри `query_processor.cpp` (анонимный namespace).
- [ ] Сохранить обработку `USE database` до Phase 1 (уже сделано).

### 7.2. Контракт ошибок (согласовать с человеком 2)

- [ ] `ParseError` → `StatusCode::kParseError` (синтаксис).
- [ ] `BindError` → `StatusCode::kBindError` (таблица/колонка не найдена).
- [ ] `PlanError` → `StatusCode::kPlanError` (логическая ошибка плана).
- [ ] `ExecutionError` → `StatusCode::kExecutionError` (runtime: deadlock, деление на ноль и т.д.).
- [ ] Semantic ошибки (например, `CREATE TABLE` в несуществующей БД) — обсудить, на чьей стороне они проверяются.

### 7.3. Формат QueryResult для разных типов запросов

- [ ] DDL (`CREATE DATABASE`, `CREATE TABLE`): `ok=true, affected_rows=0, columns/rows пустые`.
- [ ] DML (`INSERT`, `DELETE`, `UPDATE`): `ok=true, affected_rows=N, columns/rows пустые`.
- [ ] SELECT: `ok=true, columns=[...], rows=[...], affected_rows=rows.size()`.
- [ ] Ошибка: `ok=false, error="...", affected_rows=0`.

## Этап 8. Pipeline end-to-end

Цель: весь путь от CLI до Response работает как система, даже если `QueryProcessor` пока возвращает stub.

- [ ] CLI → TcpClient: отправка SQL-строки.
- [ ] TcpServer → StorageService: диспатч запроса, восстановление SessionContext.
- [ ] StorageService → QueryProcessor: вызов Execute.
- [ ] QueryProcessor → заглушки Phase 1/2/3 → возврат stub-ответа.
- [ ] StorageService → Response → TcpServer → TcpClient → CLI.
- [ ] Проверить, что pipeline не падает на любой входной строке (даже мусор).

## Этап 9. Интеграционные тесты pipeline

Цель: зафиксировать поведение системы на уровне границ модулей.

- [ ] Запрос ушёл и вернулся: любая строка SQL доходит до CLI и возвращает `Response`.
- [ ] Ошибки красиво возвращаются: `Response.ok=false`, `Response.error` не пустой.
- [ ] `QueryResult` стабилен: пустой, с колонками, с строками, с ошибкой — сериализуется/десериализуется корректно.
- [ ] `Response` формируется одинаково для `success` и `failure`.
- [ ] `USE database` и `SessionContext.current_db` живут корректно между запросами (проверить через несколько последовательных запросов с одним `client_id`).
- [ ] Аутентификация: пустой `auth_token` → `authenticated=false`, непустой → `authenticated=true`.

## Этап 10. Интеграция с человеком 1 (StorageNodeEngine)

**Когда готов:** API `StorageNodeEngine` от человека 1.

- [ ] Заменить `StorageNodeEngine*` в `Database` на полноценный объект или `unique_ptr` (потребуется хедер человека 1).
- [ ] Пробросить вызовы из `QueryProcessor` (Phase 3) в `db_->engine().*`: `CreateDatabase`, `CreateTable`, `Insert`, `ScanTable`, `Delete` и т.д.
- [ ] Тест: `Database` стартует с реальным engine.
- [ ] Тест: персистентность — данные survive перезапуск `Database`.

## Этап 11. Интеграция с человеком 2 (Parser / Planner / Executor)

**Когда готовы:** модули parser, planner, execution от человека 2.

- [ ] Заменить заглушки `Parse()` / `Plan()` / `ExecutePlan()` на реальные вызовы.
- [ ] Согласовать формат AST (что передаёт Parser → Planner).
- [ ] Согласовать формат PhysicalPlan (что передаёт Planner → Executor).
- [ ] Согласовать обработку ошибок: где ловить, как мапить в `StatusCode`.
- [ ] Убедиться, что `QueryResult` из Executor совместим с `QueryResult` из твоего контракта.

## Этап 12. End-to-end SQL тесты

Цель: полный сценарий «как у реальной БД».

- [ ] `CREATE DATABASE testdb;` → `USE testdb;` → `CREATE TABLE users (id int, name string);`
- [ ] `INSERT INTO users (id, name) VALUES (1, "alice");` → affected_rows=1.
- [ ] `SELECT * FROM users;` → 2 колонки, N строк.
- [ ] `DELETE FROM users;` → affected_rows=N.
- [ ] Перезапуск сервера → `SELECT` возвращает данные (персистентность).
- [ ] Несколько клиентов одновременно: каждый со своим `SessionContext`.

## Критерий готовности

Проект запускается одной командой (`cmake --build` + `ctest`), pipeline от CLI до engine работает, ошибки возвращаются в `Response`, интеграционные тесты проходят.
