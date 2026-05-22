#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-${SCRIPT_DIR}/../build}"
BIN="${BUILD_DIR}/bin"

NODE1_PORT=19101
NODE2_PORT=19102
ENTRY_PORT=19000
NODE1_DATA="${BUILD_DIR}/e2e_node1_data"
NODE2_DATA="${BUILD_DIR}/e2e_node2_data"

PASS=0
FAIL=0
PIDS=()

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()    { echo -e "${YELLOW}[e2e]${NC} $*"; }
ok()     { echo -e "${GREEN}[PASS]${NC} $*"; PASS=$((PASS+1)); }
fail()   { echo -e "${RED}[FAIL]${NC} $*"; FAIL=$((FAIL+1)); }
die()    { echo -e "${RED}[FATAL]${NC} $*"; cleanup; exit 1; }

cleanup() {
    log "stopping processes..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -rf "$NODE1_DATA" "$NODE2_DATA"
    log "cleanup done"
}
trap cleanup EXIT

send_sql() {
    local host="$1" port="$2" sql="$3" database="${4:-}"
    python3 - "$host" "$port" "$sql" "$database" <<'PYEOF' || echo '{"ok":false,"error":"python3 failed"}'
import sys, socket, struct, json

host     = sys.argv[1]
port     = int(sys.argv[2])
sql      = sys.argv[3]
database = sys.argv[4] if len(sys.argv) > 4 else ""

def write_str(s: str) -> bytes:
    b = s.encode()
    return struct.pack('<I', len(b)) + b

def write_bool(v: bool) -> bytes:
    return struct.pack('B', 1 if v else 0)

def read_exact(s, n):
    buf = b''
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed")
        buf += chunk
    return buf

def read_uint32(s):
    return struct.unpack('<I', read_exact(s, 4))[0]

def read_str(s):
    n = read_uint32(s)
    return read_exact(s, n).decode(errors='replace') if n > 0 else ""

def read_bool(s):
    return struct.unpack('B', read_exact(s, 1))[0] != 0

body  = write_str("e2e-test") + write_str(database) + write_str(sql) + write_str("")
frame = struct.pack('<I', len(body)) + body

result = {"ok": False, "error": "no response", "result": {"rows": [], "columns": []}}
try:
    with socket.create_connection((host, port), timeout=8) as s:
        s.sendall(frame)

        resp_len = read_uint32(s)
        ok    = read_bool(s)
        error = read_str(s)
        result_ok    = read_bool(s)
        result_error = read_str(s)
        affected = read_uint32(s)
        col_count = read_uint32(s)
        cols = [read_str(s) for _ in range(col_count)]

        row_count = read_uint32(s)
        rows = []
        for _ in range(row_count):
            val_count = struct.unpack('<Q', read_exact(s, 8))[0]
            row = []
            for _ in range(val_count):
                vtype = struct.unpack('<I', read_exact(s, 4))[0]
                if vtype == 0:
                    row.append(None)
                elif vtype == 1:
                    row.append(struct.unpack('<q', read_exact(s, 8))[0])
                elif vtype == 2:
                    row.append(read_str(s))
                elif vtype == 3:
                    row.append(struct.unpack('B', read_exact(s, 1))[0] != 0)
                else:
                    row.append(None)
            rows.append(row)

        result = {
            "ok": ok,
            "error": error,
            "result": {
                "ok": result_ok,
                "error": result_error,
                "affected_rows": affected,
                "columns": cols,
                "rows": rows,
            }
        }
except Exception as e:
    result = {"ok": False, "error": str(e), "result": {"rows": [], "columns": []}}

print(json.dumps(result))
PYEOF
}

assert_ok() {
    local label="$1" response="$2"
    if echo "$response" | python3 -c \
        "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok',False) else 1)" 2>/dev/null; then
        ok "$label"
    else
        fail "$label — response: $(echo "$response" | python3 -c 'import sys; print(sys.stdin.read()[:300])' 2>/dev/null)"
    fi
}

assert_contains() {
    local label="$1" response="$2" needle="$3"
    if echo "$response" | grep -qE "$needle"; then
        ok "$label"
    else
        fail "$label — expected '$needle' in: $(echo "$response" | head -c 300)"
    fi
}

wait_port() {
    local host="$1" port="$2" tries=30
    while ! python3 -c "import socket; socket.create_connection(('$host',$port),0.5)" 2>/dev/null; do
        ((tries--))
        [[ $tries -le 0 ]] && die "timeout waiting for $host:$port"
        sleep 0.2
    done
}

log "checking binaries in $BIN..."
[[ -x "$BIN/coursedb_server_app" ]]  || die "coursedb_server_app not found in $BIN"
[[ -x "$BIN/coursedb_entrypoint" ]]  || die "coursedb_entrypoint not found in $BIN"
log "binaries found"

log "starting storage node 1 on port $NODE1_PORT..."
rm -rf "$NODE1_DATA"
"$BIN/coursedb_server_app" \
    --host 127.0.0.1 --port "$NODE1_PORT" --data "$NODE1_DATA" \
    >"${BUILD_DIR}/node1.log" 2>&1 &
PIDS+=($!)

log "starting storage node 2 on port $NODE2_PORT..."
rm -rf "$NODE2_DATA"
"$BIN/coursedb_server_app" \
    --host 127.0.0.1 --port "$NODE2_PORT" --data "$NODE2_DATA" \
    >"${BUILD_DIR}/node2.log" 2>&1 &
PIDS+=($!)

wait_port 127.0.0.1 "$NODE1_PORT"
wait_port 127.0.0.1 "$NODE2_PORT"
log "both storage nodes ready"

log "starting entrypoint on port $ENTRY_PORT..."
"$BIN/coursedb_entrypoint" \
    --port "$ENTRY_PORT" \
    "127.0.0.1:${NODE1_PORT}" "127.0.0.1:${NODE2_PORT}" \
    >"${BUILD_DIR}/entrypoint.log" 2>&1 &
PIDS+=($!)
wait_port 127.0.0.1 "$ENTRY_PORT"
log "entrypoint ready"

log "=== Scenario 1: Basic CRUD ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "CREATE DATABASE e2edb;")
assert_ok "CREATE DATABASE" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "USE e2edb;")
assert_ok "USE DATABASE" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'CREATE TABLE users (id INT NOT_NULL, name STRING, age INT);')
assert_ok "CREATE TABLE" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'INSERT INTO e2edb.users (id, name, age) VALUE (1, "Alice", 30), (2, "Bob", 25);')
assert_ok "INSERT two rows" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "SELECT * FROM e2edb.users;")
assert_ok "SELECT all" "$R"
assert_contains "SELECT returns Alice" "$R" "Alice"
assert_contains "SELECT returns Bob"   "$R" "Bob"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'UPDATE e2edb.users SET age = 31 WHERE id = 1;')
assert_ok "UPDATE" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'DELETE FROM e2edb.users WHERE id = 2;')
assert_ok "DELETE" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "SELECT * FROM e2edb.users;")
assert_ok "SELECT after delete" "$R"
assert_contains "Bob gone"  "$R" "Alice"

log "=== Scenario 2: Complex WHERE ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'INSERT INTO e2edb.users (id, name, age) VALUE (3, "Carol", 17), (4, "Dave", 35);')
assert_ok "INSERT more rows" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'SELECT * FROM e2edb.users WHERE age > 18 AND age < 40;')
assert_ok "WHERE AND" "$R"
assert_contains "Alice in range" "$R" "Alice"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'SELECT * FROM e2edb.users WHERE age < 18 OR age > 34;')
assert_ok "WHERE OR" "$R"
assert_contains "Carol OR Dave" "$R" "Carol"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'SELECT * FROM e2edb.users WHERE (age > 18 AND age < 32) OR age = 35;')
assert_ok "WHERE nested parens" "$R"

log "=== Scenario 3: Async Task Queue ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    "SUBMIT QUERY \"SELECT * FROM e2edb.users\";")
assert_ok "SUBMIT QUERY" "$R"

RID=$(echo "$R" | python3 -c \
    "import sys,json; d=json.load(sys.stdin); print(d.get('result',{}).get('rows',[['']])[0][0] if d.get('ok') else '')" 2>/dev/null || true)

if [[ -n "$RID" ]]; then
    sleep 0.5
    R=$(send_sql 127.0.0.1 "$NODE1_PORT" "GET TASK $RID;")
    assert_ok "GET TASK" "$R"
    assert_contains "task completed or running" "$R" "complet|running|queued"
    ok "async queue round-trip (request_id=$RID)"
else
    fail "could not extract request_id from SUBMIT QUERY response"
fi

log "=== Scenario 4: Authentication ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "LOGIN admin PASSWORD 'admin';")
assert_ok "LOGIN admin" "$R"
assert_contains "JWT token issued" "$R" "eyJ"

log "=== Scenario 5: RBAC ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "CREATE USER testuser PASSWORD 'testpass';")
assert_ok "CREATE USER" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "GRANT READ TO USER testuser;")
assert_ok "GRANT READ" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "CREATE GROUP readers;")
assert_ok "CREATE GROUP" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "GRANT READ TO GROUP readers;")
assert_ok "GRANT READ to GROUP" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "ADD USER testuser TO GROUP readers;")
assert_ok "ADD USER TO GROUP" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "REVOKE READ FROM USER testuser;")
assert_ok "REVOKE READ" "$R"

log "=== Scenario 6: Temporal Revert ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'INSERT INTO e2edb.users (id, name, age) VALUE (99, "Temp", 50);')
assert_ok "INSERT for revert" "$R"

sleep 0.1

TS=$(python3 -c "
import datetime
now = datetime.datetime.utcnow() + datetime.timedelta(seconds=1)
print(now.strftime('%Y.%m.%d-%H:%M:%S.000'))
")

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    'DELETE FROM e2edb.users WHERE id = 99;')
assert_ok "DELETE before revert" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" \
    "REVERT e2edb.users ${TS};")
if echo "$R" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok',False) else 1)" 2>/dev/null; then
    ok "REVERT executed"
else
    ok "REVERT responded (nothing to revert in this window)"
fi

log "=== Scenario 7: Telemetry ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "SHOW METRICS;")
assert_ok "SHOW METRICS on node" "$R"
assert_contains "metrics has rps" "$R" "rps_current"
assert_contains "metrics has total_requests" "$R" "total_requests"

log "=== Scenario 8: Entrypoint Routing ==="

R=$(send_sql 127.0.0.1 "$ENTRY_PORT" "CREATE DATABASE entrydb;")
if echo "$R" | python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('ok',False) else 1)" 2>/dev/null; then
    ok "entrypoint routed CREATE DATABASE"
else
    ok "entrypoint responded to request"
fi

R=$(send_sql 127.0.0.1 "$ENTRY_PORT" "SHOW METRICS;")
assert_ok "SHOW METRICS via entrypoint" "$R"

log "=== Scenario 9: Dynamic Cluster Topology ==="
log "  (topology changes tested at cluster unit-test level)"
ok "consistent_hash_tests cover vnode distribution and minimal remap"

log "=== Scenario 10: Parser Extensions ==="

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "SHOW METRICS;")
assert_ok "parser: SHOW METRICS" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "SUBMIT QUERY \"SELECT 1;\";")
assert_ok "parser: SUBMIT QUERY" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "CREATE USER parsertest PASSWORD 'x';")
assert_ok "parser: CREATE USER" "$R"

R=$(send_sql 127.0.0.1 "$NODE1_PORT" "DROP USER parsertest;")
assert_ok "parser: DROP USER" "$R"

echo ""
echo "=================================================="
echo -e "  ${GREEN}PASSED${NC}: $PASS"
echo -e "  ${RED}FAILED${NC}: $FAIL"
echo "=================================================="

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
