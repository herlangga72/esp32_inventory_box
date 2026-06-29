---
type: API Reference
title: REST API
description: Complete HTTP API reference for the ESP32 Inventory Box — all endpoints, request/response schemas, and error formats.
tags: [api, rest, http, json]
timestamp: 2026-06-29T00:00:00Z
---

# REST API

Base URL: `http://<esp32-ip>/` (STA) or `http://192.168.4.1/` (AP config portal).
All responses `application/json`. Error format: `{"error":true,"code":<http>,"message":"<reason>"}`.

## System

### `GET /api/status` — Full system snapshot

```json
{
  "connected": true, "apMode": false,
  "ipAddress": "192.168.1.100", "wifiRssi": -45, "wifiSSID": "MyWiFi",
  "systemStatus": "OK", "hasErrors": false,
  "state": "IDLE", "contents": 2,
  "currentUser": 1, "currentUserName": "Alice",
  "contentsList": [{"id":1,"name":"Hammer","weight":450.0}],
  "weight": 570.0, "baseline": 0.0, "delta": 570.0,
  "uptime": 123456, "freeHeap": 204800
}
```

### `GET /api/diagnostics` — Per-component health

```json
{
  "overallStatus": "WARNING", "uptime": 123456,
  "totalErrors": 3, "lastError": "WiFi: Connection failed",
  "okCount": 7, "warningCount": 1, "errorCount": 2,
  "components": [
    {"name":"Storage","status":"OK","lastError":"","errorCount":0},
    {"name":"WiFi","status":"WARNING","lastError":"Connection failed","errorCount":1}
  ]
}
```

### `POST /api/restart` — Reboot ESP32

Response: `{"success":true,"message":"Restarting..."}` (500ms delay, then reboot)

### `GET /api/config` — Current config

```json
{"deviceName":"ESP32-Inventory-Box","wifiSSID":"MyWiFi","wifiRssi":-45,
 "freeHeap":204800,"uptime":123456,"logLevel":3}
```

### `POST /api/config` — Update config

```json
{"threshold":2.0,"settlingTime":3000,"motionThreshold":0.15,
 "lightSleepTimeout":10000,"deepSleepTimeout":60000,
 "defaultTolerance":5.0,"maxContents":10,"logLevel":3,
 "factoryReset":false}
```

All fields optional. Persisted to NVS. `factoryReset:true` → storage.clear() + reboot.

## Tools

### `GET /api/tools` — List all

```json
{"tools":[{"id":1,"name":"Hammer","weight":450.0,"tolerance":5.0,"active":true}]}
```

### `POST /api/tools` — Create

Request: `{"name":"Hammer","weight":450.0,"tolerance":5.0}` (tolerance optional, default 5.0)
Response: `{"success":true,"id":1}`

### `GET /api/tools/{id}` — Get one

```json
{"id":1,"name":"Hammer","weight":450.0,"tolerance":5.0,"active":true}
```

### `PUT /api/tools/{id}` — Update

Request: same as POST (all fields optional)
Response: `{"success":true}`

### `DELETE /api/tools/{id}` — Delete

Response: `{"success":true}` or `{"error":true,"code":404,"message":"Tool not found"}`

## Users

### `GET /api/users` — List all (PINs hidden)

```json
{"users":[{"id":1,"name":"Alice","active":true,"fpId":5}]}
```

### `POST /api/users` — Create

Request: `{"name":"Alice","pin":"1234"}`
Response: `{"success":true,"id":1}`

### `POST /api/users/login` — PIN login

Request: `{"pin":"1234"}`
Success: `{"success":true,"userId":1,"name":"Alice"}`
Fail: `{"error":true,"code":401,"message":"Invalid PIN"}`

### `POST /api/users/logout` — Clear current user

Response: `{"success":true}`

### `DELETE /api/users/{id}` — Delete user

Response: `{"success":true}`

## Logs

### `GET /api/logs?limit=50&offset=0` — Paginated logs

```json
{"logs":[{"ts":123456,"level":3,"tag":"STATE","msg":"TOOL_PLACED...",
  "userId":1,"toolId":1,"weight":450.0}],
 "total":500,"dropped":0,"fileSize":24576}
```

### `GET /api/logs/download` — Full CSV (`text/csv`)

### `POST /api/logs/clear` — Clear all logs

Response: `{"success":true}`

## Calibration

### `POST /api/calibrate` — Save current weight as baseline

Response: `{"success":true,"baseline":0.0}`

Sends CALIBRATION message to StateManager, persists to NVS.

## WiFi

### `GET /api/wifi` — WiFi status

```json
{"connected":true,"apMode":false,"ip":"192.168.1.100","ssid":"MyWiFi","rssi":-45}
```

### `POST /api/wifi` — Set credentials

Request: `{"ssid":"MyWiFi","password":"secret"}`
Response: `{"success":true,"message":"Credentials saved. Rebooting..."}` (1.5s delay, reboot)

### `GET /scan` — WiFi site survey (async)

First call: starts scan → `{"networks":[]}`
Complete: `{"networks":[{"ssid":"MyWiFi","rssi":-45}]}`

## Access Control

### `GET /api/access/status` — Access state

```json
{
  "state":"idle","lastEvent":"granted fpId=5 userId=1 user=Alice reason=server_approved",
  "lastFpId":5,"localFallback":true,
  "serverStatus":0,"serverFailDuration":0,"serverLatency":120,"enrolling":false
}
```

Server status: 0=reachable, 1=unreachable, 2=not configured.

### `GET /api/access/server` — Server config

```json
{"serverUrl":"http://192.168.1.50:8080","serverToken":"***hidden***",
 "configured":true,"reachable":true,"localFallback":true}
```

### `POST /api/access/server` — Update server config

Request: `{"serverUrl":"http://...","serverToken":"my-token","localFallback":true}`

### `POST /api/fingerprint/enroll` — Start enrollment

Request: `{"fpId":10,"userId":1}` (userId optional, links fpId to user)
Response: `{"success":true,"fpId":10,"userId":1,"message":"Enrollment started..."}`

### `GET /api/fingerprint/enroll/status` — Poll enrollment

```json
{"enrolling":true,"step":1,"fpId":10,"stepName":"second_capture"}
```

Step names: idle(-1), failed(-2), waiting(0), second_capture(1), complete(2).

### `POST /api/fingerprint/delete` — Delete template

Request: `{"fpId":10}`

## Door

### `GET /api/door` — Door state

```json
{"doorState":"locked","doorOpen":false,"state":0,"accessState":"idle"}
```

### `POST /api/door/unlock` — Remote unlock

Success: `{"success":true,"message":"Door unlocking"}`
Fail: `{"error":true,"code":409,"message":"Door busy or not available"}`

## SPIFFS Static Files

| Route | File | MIME |
|-------|------|------|
| `/` or `/index.html` | `index.html` | text/html |
| `/styles.css` | `styles.css` | text/css |
| `/app.js` | `app.js` | application/javascript |
| `/pages/dashboard.html` | `pages/dashboard.html` | text/html |
| `/pages/tools.html` | `pages/tools.html` | text/html |
| `/pages/users.html` | `pages/users.html` | text/html |
| `/pages/logs.html` | `pages/logs.html` | text/html |
| `/pages/diagnostics.html` | `pages/diagnostics.html` | text/html |
| `/pages/config.html` | `pages/config.html` | text/html |
| `/pages/wifi.html` | `pages/wifi.html` | text/html |

SPIFFS reads protected by `spiffsLock()`/`spiffsUnlock()`.

## Additional Endpoints

### `POST /api/contents/clear` — Clear box contents list

# Citations

[1] docs/web-api.md — Full API documentation
[2] src/presentation/WebServer.cpp — Implementation
