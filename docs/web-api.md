# Web API Reference

Base URL: `http://<esp32-ip>/` (STA mode) or `http://192.168.4.1/` (AP config portal only).

All responses are `application/json` unless noted. Error responses: `{"error":true,"code":<http_code>,"message":"<reason>"}`.

---

## System

### `GET /api/status`
Full system state snapshot.

**Response**:
```json
{
  "connected": true,
  "apMode": false,
  "ipAddress": "192.168.1.100",
  "wifiRssi": -45,
  "wifiSSID": "MyWiFi",
  "systemStatus": "OK",
  "hasErrors": false,
  "state": "IDLE",
  "contents": 2,
  "currentUser": 1,
  "currentUserName": "Alice",
  "contentsList": [
    {"id": 1, "name": "Hammer", "weight": 450.0},
    {"id": 3, "name": "Screwdriver", "weight": 120.0}
  ],
  "weight": 570.0,
  "baseline": 0.0,
  "delta": 570.0,
  "uptime": 123456,
  "freeHeap": 204800
}
```

### `GET /api/diagnostics`
Per-component health status.

**Response**:
```json
{
  "overallStatus": "WARNING",
  "uptime": 123456,
  "totalErrors": 3,
  "lastError": "WiFi: Connection failed",
  "okCount": 7,
  "warningCount": 1,
  "errorCount": 2,
  "components": [
    {"name": "Storage", "status": "OK", "lastError": "", "errorCount": 0},
    {"name": "HX711", "status": "OK", "lastError": "", "errorCount": 0},
    {"name": "MPU6050", "status": "ERROR", "lastError": "I2C address 0x68 not found", "errorCount": 3},
    {"name": "WiFi", "status": "WARNING", "lastError": "Connection failed", "errorCount": 1}
  ]
}
```

### `POST /api/restart`
Reboots ESP32.

**Response**: `{"success":true,"message":"Restarting..."}` (then 500ms delay, then reboot)

---

## Tools

### `GET /api/tools`
List all tools.

**Response**:
```json
{
  "tools": [
    {"id": 1, "name": "Hammer", "weight": 450.0, "tolerance": 5.0, "active": true},
    {"id": 2, "name": "Wrench", "weight": 320.0, "tolerance": 3.0, "active": true}
  ]
}
```

### `POST /api/tools`
Create a tool.

**Request**:
```json
{"name": "Hammer", "weight": 450.0, "tolerance": 5.0}
```
`tolerance` optional — defaults to `Config::DEFAULT_TOLERANCE` (5.0).

**Response**: `{"success":true,"id":1}`

### `GET /api/tools/{id}`
Get single tool.

**Response**: `{"id":1,"name":"Hammer","weight":450.0,"tolerance":5.0,"active":true}`

### `PUT /api/tools/{id}`
Update tool.

**Request**: same as POST. All fields optional — only provided fields updated.

**Response**: `{"success":true}`

### `DELETE /api/tools/{id}`
Delete tool.

**Response**: `{"success":true}` or `{"error":true,"code":404,"message":"Tool not found"}`

---

## Users

### `GET /api/users`
List all users. PINs not exposed.

**Response**:
```json
{
  "users": [
    {"id": 1, "name": "Alice", "active": true, "fpId": 5},
    {"id": 2, "name": "Bob", "active": true, "fpId": 0}
  ]
}
```

### `POST /api/users`
Create a user.

**Request**:
```json
{"name": "Alice", "pin": "1234"}
```

**Response**: `{"success":true,"id":1}`

### `POST /api/users/login`
PIN-based login. Sets current user in StateManager.

**Request**: `{"pin": "1234"}`

**Response (success)**: `{"success":true,"userId":1,"name":"Alice"}`
**Response (fail)**: `{"error":true,"code":401,"message":"Invalid PIN"}`

### `POST /api/users/logout`
Clear current user.

**Response**: `{"success":true}`

### `DELETE /api/users/{id}`
Delete user.

**Response**: `{"success":true}`

---

## Logs

### `GET /api/logs?limit=50&offset=0`
Paginated log entries.

**Response**:
```json
{
  "logs": [
    {"ts": 123456, "level": 3, "tag": "STATE", "msg": "TOOL_PLACED uid=1 toolId=1 w=450.0 d=450.0", "userId": 1, "toolId": 1, "weight": 450.0}
  ],
  "total": 500,
  "dropped": 0,
  "fileSize": 24576
}
```

### `GET /api/logs/download`
Full log as CSV (`text/csv`).

### `POST /api/logs/clear`
Clear all logs.

**Response**: `{"success":true}`

---

## Calibration

### `POST /api/calibrate`
Save current weight reading as baseline. Sends CALIBRATION message to StateManager.

**Response**:
```json
{"success":true,"baseline":0.0}
```

---

## Configuration

### `GET /api/config`
Current runtime config.

**Response**:
```json
{
  "deviceName": "ESP32-Inventory-Box",
  "wifiSSID": "MyWiFi",
  "wifiRssi": -45,
  "freeHeap": 204800,
  "uptime": 123456,
  "logLevel": 3
}
```

### `POST /api/config`
Update runtime config. All fields optional.

**Request**:
```json
{
  "threshold": 2.0,
  "settlingTime": 3000,
  "motionThreshold": 0.15,
  "lightSleepTimeout": 10000,
  "deepSleepTimeout": 60000,
  "defaultTolerance": 5.0,
  "maxContents": 10,
  "logLevel": 3,
  "factoryReset": false
}
```

Configs persisted to NVS. `factoryReset: true` → `storage.clear()` + reboot.

**Response**: `{"success":true}`

---

## WiFi

### `GET /api/wifi`
WiFi status.

**Response**:
```json
{"connected":true,"apMode":false,"ip":"192.168.1.100","ssid":"MyWiFi","rssi":-45}
```

### `POST /api/wifi`
Set WiFi credentials. Triggers reboot after save.

**Request**:
```json
{"ssid": "MyWiFi", "password": "secret"}
```

**Response**: `{"success":true,"message":"Credentials saved. Rebooting..."}` (then 1.5s delay, reboot)

### `GET /scan`
WiFi site survey (async, non-blocking).

**First call**: starts scan, returns `{"networks":[]}`
**Subsequent calls** (while scanning): `{"networks":[]}`
**When complete**:
```json
{
  "networks": [
    {"ssid": "MyWiFi", "rssi": -45},
    {"ssid": "NeighborNet", "rssi": -78}
  ]
}
```

---

## Access Control

### `GET /api/access/status`
Access controller state.

**Response**:
```json
{
  "state": "idle",
  "lastEvent": "granted fpId=5 userId=1 user=Alice reason=server_approved",
  "lastFpId": 5,
  "localFallback": true,
  "serverStatus": 0,
  "serverFailDuration": 0,
  "serverLatency": 120,
  "enrolling": false
}
```

Server status: `0` = reachable, `1` = unreachable, `2` = not configured.

If enrolling:
```json
{
  "enrolling": true,
  "enrollStep": 1,
  "enrollFpId": 10
}
```

### `GET /api/access/server`
Server client configuration.

**Response**:
```json
{
  "serverUrl": "http://192.168.1.50:8080",
  "serverToken": "***hidden***",
  "configured": true,
  "reachable": true,
  "localFallback": true
}
```

### `POST /api/access/server`
Update server configuration.

**Request**:
```json
{
  "serverUrl": "http://192.168.1.50:8080",
  "serverToken": "my-auth-token",
  "localFallback": true
}
```

**Response**: `{"success":true}`

### `POST /api/fingerprint/enroll`
Start fingerprint enrollment.

**Request**: `{"fpId": 10}` (1-127)

**Response**:
```json
{"success":true,"fpId":10,"message":"Enrollment started. Place finger on sensor."}
```

### `GET /api/fingerprint/enroll/status`
Poll enrollment progress.

**Response**:
```json
{
  "enrolling": true,
  "step": 1,
  "fpId": 10,
  "stepName": "second_capture"
}
```

Step names: `idle` (-1), `failed` (-2), `waiting` (0), `second_capture` (1), `complete` (2).

### `POST /api/fingerprint/delete`
Delete a fingerprint template.

**Request**: `{"fpId": 10}`

**Response**: `{"success":true}`

---

## Door

### `GET /api/door`
Door state.

**Response**:
```json
{
  "doorState": "locked",
  "doorOpen": false,
  "state": 0,
  "accessState": "idle"
}
```

### `POST /api/door/unlock`
Remote unlock (admin override).

**Response**: `{"success":true,"message":"Door unlocking"}`

Error (door busy): `{"error":true,"code":409,"message":"Door busy or not available"}`

---

## SPIFFS Static Files

| Route | File | MIME |
|-------|------|------|
| `/` or `/index.html` | `index.html` | `text/html` |
| `/styles.css` | `styles.css` | `text/css` |
| `/app.js` | `app.js` | `application/javascript` |
| `/pages/dashboard.html` | `pages/dashboard.html` | `text/html` |
| `/pages/tools.html` | `pages/tools.html` | `text/html` |
| `/pages/users.html` | `pages/users.html` | `text/html` |
| `/pages/logs.html` | `pages/logs.html` | `text/html` |
| `/pages/diagnostics.html` | `pages/diagnostics.html` | `text/html` |
| `/pages/config.html` | `pages/config.html` | `text/html` |
| `/pages/wifi.html` | `pages/wifi.html` | `text/html` |

All SPIFFS reads protected by `spiffsLock()`/`spiffsUnlock()` for thread safety.
