#!/usr/bin/env bash
# API endpoint test suite for ESP32 Inventory Box
# Usage: ./test-api.sh [host:port]    (default: localhost:8080)
# Run `make serve` first for local testing, or point at real hardware.

BASE="${1:-localhost:8080}"
PASS=0
FAIL=0

green() { printf "  \033[32mPASS\033[0m %s\n" "$1"; ((PASS++)); }
red()   { printf "  \033[31mFAIL\033[0m %s\n" "$1"; ((FAIL++)); }
skip()  { printf "  \033[33mSKIP\033[0m %s\n" "$1"; }

check_status() {
    local code="$1" expected="$2" label="$3"
    if [ "$code" = "$expected" ]; then
        green "$label"
    else
        red "$label (HTTP $code, expected $expected)"
    fi
}

check_json() {
    local body="$1" key="$2" label="$3"
    if echo "$body" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$key'))" 2>/dev/null | grep -q .; then
        green "$label"
    else
        red "$label (missing key: $key)"
    fi
}

echo "=== ESP32 Inventory Box API Test Suite ==="
echo "Target: $BASE"
echo ""

# ---- GET endpoints ----
echo "--- GET ---"

# /api/status
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/status")
check_status "$r" 200 "GET /api/status → 200"
body=$(curl -s "http://$BASE/api/status")
check_json "$body" "state" "  /api/status has 'state'"
check_json "$body" "weight" "  /api/status has 'weight'"
check_json "$body" "freeHeap" "  /api/status has 'freeHeap'"

# /api/tools
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/tools")
check_status "$r" 200 "GET /api/tools → 200"
body=$(curl -s "http://$BASE/api/tools")
check_json "$body" "tools" "  /api/tools has 'tools'"

# /api/users
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/users")
check_status "$r" 200 "GET /api/users → 200"
body=$(curl -s "http://$BASE/api/users")
check_json "$body" "users" "  /api/users has 'users'"

# /api/logs
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/logs?limit=10&offset=0")
check_status "$r" 200 "GET /api/logs → 200"
body=$(curl -s "http://$BASE/api/logs?limit=10&offset=0")
check_json "$body" "logs" "  /api/logs has 'logs'"

# /api/logs/download
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/logs/download")
check_status "$r" 200 "GET /api/logs/download → 200"
ct=$(curl -s -D - "http://$BASE/api/logs/download" 2>/dev/null | grep "Content-Type" | tr -d '\r')
if echo "$ct" | grep -qi "csv"; then
    green "  /api/logs/download Content-Type is CSV"
else
    red "  /api/logs/download Content-Type not CSV: $ct"
fi

# /api/diagnostics
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/diagnostics")
check_status "$r" 200 "GET /api/diagnostics → 200"
body=$(curl -s "http://$BASE/api/diagnostics")
check_json "$body" "components" "  /api/diagnostics has 'components'"
check_json "$body" "overallStatus" "  /api/diagnostics has 'overallStatus'"

# /api/config
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/config")
check_status "$r" 200 "GET /api/config → 200"
body=$(curl -s "http://$BASE/api/config")
check_json "$body" "logLevel" "  /api/config has 'logLevel'"

# /api/wifi
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/wifi")
check_status "$r" 200 "GET /api/wifi → 200"
body=$(curl -s "http://$BASE/api/wifi")
check_json "$body" "ssid" "  /api/wifi has 'ssid'"
check_json "$body" "rssi" "  /api/wifi has 'rssi'"

# /api/access/status
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/access/status")
check_status "$r" 200 "GET /api/access/status → 200"

# /api/access/server
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/access/server")
check_status "$r" 200 "GET /api/access/server → 200"

# /api/fingerprint/enroll/status
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/fingerprint/enroll/status")
check_status "$r" 200 "GET /api/fingerprint/enroll/status → 200"

# /api/door
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/door")
check_status "$r" 200 "GET /api/door → 200"

# /scan
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/scan")
check_status "$r" 200 "GET /scan → 200"

echo ""

# ---- POST endpoints ----
echo "--- POST ---"

# POST /api/calibrate
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/calibrate")
check_status "$r" 200 "POST /api/calibrate → 200"
body=$(curl -s -X POST "http://$BASE/api/calibrate")
check_json "$body" "baseline" "  /api/calibrate has 'baseline'"

# POST /api/restart
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/restart")
check_status "$r" 200 "POST /api/restart → 200"

# POST /api/tools (create)
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/tools" \
    -H "Content-Type: application/json" -d '{"name":"test","weight":100,"tolerance":5}')
check_status "$r" 200 "POST /api/tools → 200"

# POST /api/users (create)
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/users" \
    -H "Content-Type: application/json" -d '{"name":"test","pin":"1234"}')
check_status "$r" 200 "POST /api/users → 200"

# POST /api/users/login
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/users/login" \
    -H "Content-Type: application/json" -d '{"pin":"1234"}')
check_status "$r" 200 "POST /api/users/login → 200"
body=$(curl -s -X POST "http://$BASE/api/users/login" \
    -H "Content-Type: application/json" -d '{"pin":"1234"}')
check_json "$body" "userId" "  /api/users/login has 'userId'"

# POST /api/users/logout
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/users/logout")
check_status "$r" 200 "POST /api/users/logout → 200"

# POST /api/logs/clear
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/logs/clear")
check_status "$r" 200 "POST /api/logs/clear → 200"

# POST /api/config
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/config" \
    -H "Content-Type: application/json" -d '{"logLevel":3}')
check_status "$r" 200 "POST /api/config → 200"

# POST /api/wifi (save credentials)
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/wifi" \
    -H "Content-Type: application/json" -d '{"ssid":"Test","password":"test123"}')
check_status "$r" 200 "POST /api/wifi → 200"

# POST /api/contents/clear
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/contents/clear")
check_status "$r" 200 "POST /api/contents/clear → 200"

# POST /api/access/server
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/access/server" \
    -H "Content-Type: application/json" -d '{"serverUrl":"http://test"}')
check_status "$r" 200 "POST /api/access/server → 200"

# POST /api/door/unlock
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/door/unlock")
check_status "$r" 200 "POST /api/door/unlock → 200"

echo ""

# ---- PUT endpoints ----
echo "--- PUT ---"

# PUT /api/tools/1
r=$(curl -s -o /dev/null -w "%{http_code}" -X PUT "http://$BASE/api/tools/1" \
    -H "Content-Type: application/json" -d '{"name":"updated","weight":150,"tolerance":3}')
check_status "$r" 200 "PUT /api/tools/1 → 200"

# PUT /api/users/1
r=$(curl -s -o /dev/null -w "%{http_code}" -X PUT "http://$BASE/api/users/1" \
    -H "Content-Type: application/json" -d '{"name":"updated"}')
check_status "$r" 200 "PUT /api/users/1 → 200"

echo ""

# ---- DELETE endpoints ----
echo "--- DELETE ---"

# DELETE /api/tools/1
r=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "http://$BASE/api/tools/1")
check_status "$r" 200 "DELETE /api/tools/1 → 200"

# DELETE /api/users/1
r=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "http://$BASE/api/users/1")
check_status "$r" 200 "DELETE /api/users/1 → 200"

echo ""

# ---- Error handling ----
echo "--- Error handling ---"

# POST to unknown endpoint → 404
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/nonexistent")
check_status "$r" 404 "POST /api/nonexistent → 404"

# Invalid JSON → should not crash
r=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://$BASE/api/tools" \
    -H "Content-Type: application/json" -d '{invalid}')
if [ "$r" = "400" ] || [ "$r" = "200" ]; then
    green "POST /api/tools with invalid JSON → $r (400 on real HW)"
else
    red "POST /api/tools with invalid JSON → $r (expected 400 on real HW)"
fi

# GET nonexistent page
r=$(curl -s -o /dev/null -w "%{http_code}" "http://$BASE/api/nonexistent")
if [ "$r" = "404" ] || [ "$r" = "400" ]; then
    green "GET /api/nonexistent → $r"
else
    red "GET /api/nonexistent → $r (expected 4xx)"
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
