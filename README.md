# CourseDB

An educational relational database management system implemented in C++20. CourseDB provides a full SQL execution pipeline — from lexing and parsing through planning and execution — backed by a B\*-tree storage engine, MVCC-style versioning, row-level transactions, JWT-based authentication, and a TCP network layer.

---

## Features

- **Full SQL pipeline**: `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `CREATE TABLE`, `DROP TABLE`, `CREATE DATABASE`, `DROP DATABASE`
- **B\*-tree indexes**: paged, file-backed, with point lookup and range search
- **MVCC versioning**: append-only version log with point-in-time `REVERT`
- **Transactions**: `BEGIN` / `COMMIT` / `ROLLBACK` with configurable isolation levels and row-level locking
- **Binary TCP protocol**: custom request/response framing over TCP
- **Authentication**: HS256 JWT tokens + PBKDF2-HMAC-SHA256 password hashing
- **RBAC**: users, groups, and fine-grained permissions (`READ`, `WRITE`, `CREATE_TABLE`, `DROP_TABLE`, `DROP_DATABASE`, `ALTER_TABLE`, `ADMIN`)
- **Async task queue**: WAL-backed, thread-pool-driven async SQL execution with UUID task IDs
- **Cluster routing**: consistent-hash ring (MurmurHash3) with an entrypoint proxy node
- **Telemetry**: sliding-window RPS, latency percentiles (p95/p99), and error rates
- **Structured logging**: async JSON Lines log with rotation and access log

---

## Architecture

```
coursedb_common          <- Status, Value, Bytes, Config, serialization
coursedb_storage         <- PageManager, RowStore, TableStorage, CatalogManager,
                           BStarTree, IndexManager, VersionLog, StorageNodeEngine
coursedb_parser          <- Lexer -> Token -> Parser -> AST / SqlStatement
coursedb_planner         <- Binder -> LogicalPlan -> Optimizer -> PhysicalPlan
coursedb_execution       <- Executor hierarchy (SeqScan, IndexScan, Insert,
                           Update, Delete, CreateTable, CreateIndex)
coursedb_transaction     <- Transaction, LockManager, TransactionManager
coursedb_network         <- Protocol, Request/Response, Session, TcpServer/TcpClient
coursedb_auth            <- JwtAuth (HS256), RbacStore (PBKDF2 + JSON file)
coursedb_queue           <- TaskQueue (WAL-backed, worker pool)
coursedb_logging         <- Logger (async MPSC queue, JSON Lines, rotation)
coursedb_telemetry       <- NodeMetrics, ClusterMetrics, MetricsSnapshot
coursedb_cluster         <- ConsistentHashRing (MurmurHash3), EntrypointNode
coursedb_server          <- Database, QueryProcessor, StorageService, SessionContext
```

### SQL Query Pipeline

`QueryProcessor` drives the full execution pipeline:

1. **Parser** (`Lexer` → `Parser`) produces a `SqlStatement`
2. **Binder** resolves table and column names against `CatalogManager` → `LogicalPlan`
3. **Optimizer** rewrites the plan (index selection, pushdowns)
4. **Planner** converts to a `PhysicalPlan`
5. **Executor** tree executes the plan against `StorageNodeEngine`

### Storage Engine

`StorageNodeEngine` (in `include/runtime/storage_node_engine.h`) is the central storage facade. It composes:

- `CatalogManager` — persists database/table/index schemas to disk
- `IndexManager` — manages `BStarTree` instances (one file per index)
- `TableStorage` / `RowStore` / `PageManager` — page-based heap file storage (default 4 KB pages)
- `VersionLog` — append-only log of before/after row images used by `RevertService`

`*Internal` methods (e.g., `InsertInternal`) perform the raw storage write without writing to the version log. `RevertService` calls these directly when replaying or undoing changes.

---

## Data Directory Layout

All server artifacts are stored under a single root directory (default `./data`). The layout is created automatically on first start:

```
data/                         ← root data directory (--data flag)
├── <dbname>/                 ← one directory per database
│   ├── catalog/              ← table metadata (.meta files)
│   │   └── <table>.meta
│   ├── tables/               ← heap storage files
│   │   └── <table>.tbl
│   ├── indexes/              ← B*-tree index files
│   │   └── <table>/
│   │       └── <index>.idx
│   └── versions/             ← version log files for REVERT
│       └── <table>.vlog
├── task_queue/               ← WAL for async task queue
│   └── tasks.wal
├── logs/                     ← structured JSON logs
│   ├── server.log            ← general + debug log (JSON Lines)
│   └── access.log            ← per-request access log (JSON Lines)
└── rbac.json                 ← user/group/role store (atomically written)
```

> **Note**: the old default placed logs in `./logs/` (outside the data directory). Since v2 all paths are derived from `--data`, keeping every artifact in one place.

---

## Project Structure

```
CourseDB/
├── include/           # Public headers, organized by module
│   ├── auth/          # JwtAuth, RbacStore
│   ├── catalog/       # Schema, TableDescriptor, IndexDescriptor, CatalogManager
│   ├── cluster/       # ConsistentHashRing, EntrypointNode
│   ├── common/        # Status, Bytes, Config, BinaryIO, serialization
│   ├── execution/     # Value, Tuple, Expression, Executor hierarchy
│   ├── index/         # BStarTree, IndexManager, KeyEncoder
│   ├── logging/       # Logger, AccessLogEntry
│   ├── network/       # Request, Response, Protocol, TcpServer, TcpClient, Session
│   ├── parser/        # Token, Lexer, Parser, AST, SqlStatement
│   ├── planner/       # Binder, LogicalPlan, PhysicalPlan, Optimizer, Planner
│   ├── queue/         # TaskQueue, TaskInfo, TaskStatus
│   ├── runtime/       # StorageNodeEngine
│   ├── server/        # Database, QueryProcessor, StorageService, SessionContext
│   ├── storage/       # Page, PageManager, Row, RowStore, TableStorage
│   ├── telemetry/     # NodeMetrics, ClusterMetrics, MetricsSnapshot
│   ├── transaction/   # Transaction, LockManager, TransactionManager, IsolationLevel
│   └── versioning/    # VersionLog, VersionRecord, RevertService
├── src/               # Implementations (mirrors include/ structure)
├── apps/
│   ├── server/        # Full DBMS server (coursedb_server_app)
│   ├── storage_node/  # Standalone storage node (interactive)
│   ├── cli/           # One-shot command-line SQL client
│   └── entrypoint/    # Cluster entrypoint / proxy (coursedb_entrypoint)
├── tests/             # GoogleTest suites, one directory per module
├── scripts/           # e2e.sh end-to-end test script
└── CMakeLists.txt
```

---

## Requirements

| Tool | Minimum version |
|------|-----------------|
| C++ compiler | C++20 (GCC 11+, Clang 13+, MSVC 2022+) |
| CMake | 3.20 |
| Ninja | any recent version |
| OpenSSL | 1.1+ (for PBKDF2 and JWT) |

The following dependencies are fetched automatically by CMake via `FetchContent`:

- [nlohmann/json](https://github.com/nlohmann/json)
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp)
- [GoogleTest](https://github.com/google/googletest) (tests only)

---

## Installation

```bash
# Clone the repository
git clone <repo-url> coursedb
cd coursedb

# Install system dependencies (macOS)
brew install cmake ninja openssl

# Install system dependencies (Ubuntu/Debian)
sudo apt install cmake ninja-build libssl-dev
```

---

## Building

```bash
# Configure (from the repository root)
cmake -B build -G Ninja

# Build everything
cmake --build build
```

Build options (pass via `-D` to cmake):

| Option | Default | Description |
|--------|---------|-------------|
| `COURSEDB_BUILD_TESTS` | `ON` | Build GoogleTest suites |
| `COURSEDB_BUILD_APPS` | `ON` | Build server, CLI, and cluster executables |
| `COURSEDB_ENABLE_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors |

Build artifacts are placed in `build/bin/` and `build/lib/`.

---

## Running the Server

```bash
# Defaults: host=127.0.0.1, port=7000, data=./data
./build/bin/coursedb_server_app

# Custom options
./build/bin/coursedb_server_app --host 0.0.0.0 --port 7000 --data /var/lib/coursedb

# All flags
./build/bin/coursedb_server_app --help
```

```
Options:
  --host <addr>      Listen address         (default: 127.0.0.1)
  --port <n>         Listen port            (default: 7000)
  --data <dir>       Root data directory    (default: ./data)
                     Sub-dirs created automatically:
                       <data>/wal          — write-ahead log
                       <data>/task_queue   — async task WAL
                       <data>/logs         — server + access logs
                       <data>/rbac.json    — user/group store
  --log-level <lvl>  trace|debug|info|warn|error (default: info)
  --help             Show this help
```

On startup the server creates all required subdirectories and begins writing structured JSON logs to `<data>/logs/`.

---

## Running the CLI Client

```bash
# Interactive mode (reads SQL from stdin)
./build/bin/coursedb_cli 127.0.0.1 7000

# One-shot query
./build/bin/coursedb_cli 127.0.0.1 7000 "SELECT * FROM mydb.users;"
```

---

## Running the Cluster

Start one or more storage nodes, then start the entrypoint proxy. Each node gets its own `--data` directory so all artifacts stay isolated.

```bash
# Storage node 1
./build/bin/coursedb_server_app --port 7001 --data /var/lib/coursedb/node1

# Storage node 2
./build/bin/coursedb_server_app --port 7002 --data /var/lib/coursedb/node2

# Entrypoint — routing to both nodes (positional host:port descriptors)
./build/bin/coursedb_entrypoint --port 9000 \
    127.0.0.1:7001 \
    127.0.0.1:7002
```

Entrypoint options:

```
Options:
  --host <addr>     Listen address          (default: 127.0.0.1)
  --port <n>        Entrypoint port         (default: 9000)
  --data <dir>      Root data directory     (default: ./data)
                    Logs go into <data>/logs/
  --log-level <lvl> trace|debug|info|warn|error (default: info)
  --help            Show this help

Positional args: storage node descriptors as host:port
  Example: coursedb_entrypoint --port 9000 127.0.0.1:7001 127.0.0.1:7002
```

Clients connect to the entrypoint port. The entrypoint uses consistent hashing to route each request to the appropriate storage node and performs periodic health-checks on each node.

### Adding / Removing Nodes

The consistent-hash ring is initialized from the node list provided at startup. Dynamic topology changes are applied by restarting the entrypoint with an updated node list. Because consistent hashing minimises remapping (~1/N of keys move when a node is added), only a fraction of requests route to a different node after restart.

---

## Running Tests

```bash
# Run all unit tests
cd build && ctest --output-on-failure

# Run a specific test binary
./build/bin/coursedb_storage_tests
./build/bin/coursedb_parser_tests
./build/bin/coursedb_planner_tests
./build/bin/coursedb_execution_tests
./build/bin/coursedb_transaction_tests
./build/bin/coursedb_network_tests
./build/bin/coursedb_integration_tests
./build/bin/coursedb_auth_tests
./build/bin/coursedb_queue_tests
./build/bin/coursedb_telemetry_tests
./build/bin/coursedb_cluster_tests
./build/bin/coursedb_logging_tests

# Run a single test case
./build/bin/coursedb_storage_tests --gtest_filter="BStarTreeTest.InsertAndFind"

# End-to-end integration test (starts real server processes)
bash scripts/e2e.sh
```

---

## SQL Reference

### DDL

```sql
CREATE DATABASE mydb;
USE mydb;
DROP DATABASE mydb;

CREATE TABLE users (id INT, name STRING, active BOOL);
DROP TABLE users;
```

### DML

```sql
INSERT INTO users (id, name, active) VALUES (1, 'alice', true);

SELECT * FROM users;
SELECT id, name FROM users WHERE id = 1;
SELECT * FROM users WHERE age > 18 AND active = true;
SELECT * FROM users WHERE (age < 20 OR age > 60) AND active = true;

UPDATE users SET name = 'bob' WHERE id = 1;

DELETE FROM users WHERE id = 2;
```

### Transactions

```sql
BEGIN;
INSERT INTO accounts (id, balance) VALUES (1, 1000);
UPDATE accounts SET balance = balance - 100 WHERE id = 1;
COMMIT;

BEGIN;
DELETE FROM accounts WHERE id = 2;
ROLLBACK;
```

### Versioning / REVERT

```sql
-- Revert a table to a specific point in time (wall-clock timestamp)
REVERT TABLE users TO TIMESTAMP '2024-01-15-12:00:00.000';
```

The `VersionLog` records before/after row images for every `INSERT`, `UPDATE`, and `DELETE`. `REVERT` replays the log in reverse to restore the table to its state at the given timestamp.

### Authentication

```sql
-- Login (returns a JWT token in the response)
-- Default admin credentials: admin / admin  (change in production)
LOGIN admin PASSWORD 'admin';

-- User management
CREATE USER alice PASSWORD 'secret123';
DROP USER alice;

-- Group management
CREATE GROUP editors;
ADD USER alice TO GROUP editors;

-- Permissions
GRANT WRITE TO USER alice;
GRANT READ TO GROUP editors;
REVOKE WRITE FROM USER alice;
```

Available permissions: `READ`, `WRITE`, `CREATE_TABLE`, `DROP_TABLE`, `DROP_DATABASE`, `ALTER_TABLE`, `ADMIN`.

Subsequent requests should include the JWT token returned by `LOGIN` in the auth field of the request. The token is valid for `jwt_ttl_seconds` (default 3600 s).

### Async Execution

```sql
-- Submit a query for async execution; returns a UUID task ID
SUBMIT QUERY "SELECT * FROM large_table;";

-- Poll for status / result
GET TASK 550e8400-e29b-41d4-a716-446655440000;

-- Cancel a pending task
CANCEL TASK 550e8400-e29b-41d4-a716-446655440000;
```

`GET TASK` returns a row with columns `status`, `progress`, `result`, `error`. Status values: `queued`, `running`, `completed`, `failed`, `cancelled`.

### Telemetry

```sql
SHOW METRICS;
```

Returns a JSON snapshot:

```json
{
  "node_id": "...",
  "rps_current": 42.0,
  "rps_avg_10min": 38.5,
  "rps_max_10min": 120.0,
  "latency_avg_ms": 3.2,
  "latency_p95_ms": 11.4,
  "latency_p99_ms": 28.7,
  "error_rate_1min": 0.01,
  "total_requests": 15023,
  "total_errors": 12
}
```

---

## Configuration Reference

All configuration is expressed through the `Config` struct in `include/common/config.h`. The CLI flags of `coursedb_server_app` map directly to these fields.

| Field | Default | CLI flag | Description |
|-------|---------|----------|-------------|
| `data_dir` | `./data` | `--data` | Root directory; all sub-paths derived from it |
| `wal_dir` | `./data/wal` | *(derived)* | Write-ahead log directory |
| `task_queue_dir` | `./data/task_queue` | *(derived)* | Async task WAL directory |
| `log_dir` | `./data/logs` | *(derived)* | Log file directory |
| `host` | `127.0.0.1` | `--host` | Listen address |
| `port` | `7000` | `--port` | Listen port for the storage node |
| `entrypoint_port` | `9000` | `--port` *(entrypoint)* | Listen port for the cluster entrypoint |
| `virtual_nodes_per_node` | `150` | — | Virtual nodes per physical node in the hash ring |
| `page_size` | `4096` | — | Storage page size in bytes |
| `task_queue_workers` | `4` | — | Number of async worker threads |
| `task_queue_max` | `10000` | — | Maximum queued tasks |
| `log_level` | `info` | `--log-level` | Minimum log level (`trace` … `error`) |
| `log_max_mb` | `32` | — | Log file rotation threshold (MiB) |
| `log_max_files` | `5` | — | Number of rotated log files to retain |
| `telemetry_window_seconds` | `600` | — | RPS/error sliding window duration (s) |
| `telemetry_push_interval` | `5` | — | Seconds between metric aggregation cycles |
| `jwt_secret` | `coursedb-secret-…` | — | HMAC-SHA256 signing key — **change in production** |
| `jwt_ttl_seconds` | `3600` | — | JWT token lifetime in seconds |
| `pbkdf2_iterations` | `260000` | — | PBKDF2 iteration count for password hashing |

When `--data <dir>` is passed, `wal_dir`, `task_queue_dir`, and `log_dir` are automatically set to `<dir>/wal`, `<dir>/task_queue`, and `<dir>/logs` respectively. This keeps every artifact under one root.

---

## Module Overview

### Storage (`coursedb_storage`)

The `StorageNodeEngine` facade manages all storage-level operations. It owns a `CatalogManager` (schema persistence), an `IndexManager` (B\*-tree lifecycle), and one `TableStorage` per open table (heap file + row store). All file paths are derived from `config.data_dir`:

```
data_dir/<db>/catalog/<table>.meta   ← schema
data_dir/<db>/tables/<table>.tbl     ← heap rows
data_dir/<db>/indexes/<table>/<idx>.idx  ← B*-tree
data_dir/<db>/versions/<table>.vlog  ← version log
```

The B\*-tree (`BStarTree`) stores one index per file; keys are encoded to a fixed-width binary format by `KeyEncoder` for binary comparison. `VersionLog` records before/after row images for every mutation, enabling `REVERT`.

### Parser (`coursedb_parser`)

A hand-written recursive-descent parser. `Lexer` produces a flat token stream; `Parser` consumes it and returns a `SqlStatement` subclass. The lexer supports both single- and double-quoted string literals, and tokenises hyphenated identifiers (UUIDs) as a single token.

### Planner (`coursedb_planner`)

`Binder` validates names against the catalog. `Optimizer` selects available indexes. `Planner` lowers the logical plan to a `PhysicalPlan` tree that the executor drives row-by-row.

### Network (`coursedb_network`)

Binary length-prefixed framing over TCP (POSIX on macOS/Linux). `TcpServer` accepts connections and dispatches each to a handler callback on a detached worker thread. `TcpClient` is a synchronous client used by the CLI and the entrypoint proxy.

### Auth (`coursedb_auth`)

`JwtAuth` issues and validates HS256-signed JWTs. `RbacStore` maintains an in-memory RBAC model backed by an atomically-written JSON file (`<data>/rbac.json`). Passwords are stored as `pbkdf2$<iters>$<salt_hex>$<hash_hex>` with constant-time comparison.

### Cluster (`coursedb_cluster`)

`ConsistentHashRing` maps shard keys to nodes using MurmurHash3 with configurable virtual-node counts. `EntrypointNode` accepts client connections, resolves the shard, and proxies to the target storage node. Background threads run health-checks every 5 s and metric aggregation every `telemetry_push_interval` s.

### Async Queue (`coursedb_queue`)

`TaskQueue` accepts SQL strings via `Submit()` and runs them on a fixed thread pool. Every state transition (queued → running → completed/failed) is appended to a WAL (`<data>/task_queue/tasks.wal`) and replayed on restart. Completed results are kept in memory up to `kMaxCompletedResults = 10000` entries.

### Logging (`coursedb_logging`)

`Logger` maintains an async bounded MPSC queue (65 536 slots) drained by a background writer thread. Two sinks write JSON Lines:

- `<data>/logs/server.log` — general log entries (level, message, structured fields)
- `<data>/logs/access.log` — one entry per request (timestamps, client_id, SQL, duration_ms, status)

Log rotation triggers when a file exceeds `log_max_mb`; up to `log_max_files` rotated files are retained.

---

## Troubleshooting

**Build fails: OpenSSL not found**

```bash
# macOS with Homebrew
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
cmake -B build -G Ninja -DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR
```

**`address already in use` on startup**

Another process is bound to the port. Change `--port` or terminate the conflicting process:

```bash
lsof -i :7000   # find the process
kill <pid>
```

**JWT validation fails after server restart**

Tokens are signed with `jwt_secret`. Changing the secret between restarts invalidates all issued tokens. Clients must re-authenticate with `LOGIN`.

**Data directory is empty after restart**

Ensure `--data` points to a persistent path. The default `./data` is relative to the working directory where the server was launched.

**Logs not appearing**

Check that `<data>/logs/` exists and is writable. The logger initialises asynchronously; call `Logger::Flush()` in tests or wait ~100 ms after the last write before reading log files.

---

## Known Limitations

- The storage engine has no crash-recovery WAL; a hard crash may leave pages inconsistent
- The cluster entrypoint does not rebalance existing data when nodes are added or removed
- The SQL dialect is a strict subset: no `JOIN`, no subqueries, no aggregate functions, no `ORDER BY` or `LIMIT`
- `REVERT` uses wall-clock timestamps rather than logical transaction IDs
- Async task retry logic is tracked (`retry_count` field) but not yet implemented
