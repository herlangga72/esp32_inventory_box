# ESP32 Inventory Box — Documentation

## Overview

Smart inventory tracking box using ESP32. Detects tool removal/placement via weight change + motion sensing. Tracks contents, logs events, serves web UI.

**Key Features:**
- Real-time weight monitoring (HX711 load cell)
- Motion detection (MPU6050 accelerometer)
- Auto-detect tool changes
- **Fingerprint access control with central server validation**
- **Solenoid door lock control via relay**
- **Multi-page web dashboard**
- Auto WiFi provisioning (AP fallback if no credentials)
- Robust error handling with web diagnostics
- Power management with deep sleep
- Event logging with user tracking

---

## Web UI Structure

```
/data/
├── index.html          # Main shell (layout, router, shared components)
├── styles.css          # Global styles
├── app.js              # Shared utilities, API client, global functions
└── pages/
    ├── dashboard.html   # Status overview, weight, contents
    ├── tools.html      # Tool CRUD
    ├── users.html      # User management
    ├── logs.html       # Event logs
    ├── access.html     # Access control, fingerprint, door
    ├── diagnostics.html# System health
    ├── config.html     # Settings
    └── wifi.html        # WiFi configuration
```

### Page Loading Flow

```mermaid
sequenceDiagram
    Browser->>ESP32: GET /
    ESP32->>Browser: index.html (5KB)
    
    Browser->>ESP32: GET /styles.css
    ESP32->>Browser: CSS (14KB)
    
    Browser->>ESP32: GET /app.js
    ESP32->>Browser: JS (11KB)
    
    User clicks nav link
    Browser->>ESP32: GET /pages/dashboard.html
    ESP32->>Browser: Page content + inline JS
    
    Browser executes init_dashboard()
    Page-specific JS makes API calls
```

### Memory Usage (Streaming)

```
┌─────────────────────────────────────────────────┐
│ RAM (minimal - streams from flash)              │
│  - WebServer buffer: ~4KB                       │
│  - Request parsing: ~2KB                        │
│  - JSON strings: dynamic                        │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ Flash (SPIFFS)                                  │
│  index.html      6KB                            │
│  styles.css     14KB                            │
│  app.js         11KB                           │
│  pages/                                          │
│    dashboard     4KB                           │
│    tools         6KB                           │
│    users         5KB                           │
│    logs          4KB                           │
│    access        8KB                           │
│    diagnostics    2KB                           │
│    config         5KB                           │
│    wifi          7KB                           │
│  TOTAL          ~72KB                           │
└─────────────────────────────────────────────────┘
```

---

## File Inventory

### Kernel
| File | Purpose |
|------|---------|
| `kernel/SystemStatus.h/.cpp` | Component health tracking, error reporting |
| `kernel/WiFiManager.h/.cpp` | WiFi connection + AP fallback + config portal |
| `kernel/PowerManager.h/.cpp` | Light sleep, deep sleep, wake sources |
| `kernel/ServerClient.h/.cpp` | HTTP client for central access server |

### Presentation
| File | Purpose |
|------|---------|
| `presentation/WebServer.h/.cpp` | HTTP REST API, static file serving |

### HAL
| File | Purpose |
|------|---------|
| `hal/FingerprintDriver.h/.cpp` | R307/AS608 fingerprint sensor driver |

### Domain Services
| File | Purpose |
|------|---------|
| `domain/services/AccessController.h/.cpp` | Access control orchestrator (state machine) |
| `domain/services/DoorService.h/.cpp` | Reed switch monitoring, relay control |

### Web Files
| File | Purpose |
|------|---------|
| `data/index.html` | Main shell with SPA router |
| `data/styles.css` | Global CSS variables + styles |
| `data/app.js` | Shared API client, utilities, toast notifications |
| `data/pages/*.html` | Individual page content (loaded on demand) |

---

## REST API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web UI shell |
| GET | `/styles.css` | Styles |
| GET | `/app.js` | Main JS |
| GET | `/pages/{page}.html` | Page content |
| GET | `/api/status` | System status |
| GET | `/api/diagnostics` | System health |
| POST | `/api/restart` | Restart ESP32 |
| GET | `/api/tools` | List tools |
| POST | `/api/tools` | Create tool |
| PUT | `/api/tools/{id}` | Update tool |
| DELETE | `/api/tools/{id}` | Delete tool |
| GET | `/api/users` | List users |
| POST | `/api/users` | Create user |
| POST | `/api/users/login` | Login with PIN |
| GET | `/api/logs` | Event logs |
| POST | `/api/calibrate` | Calibrate baseline |
| GET/POST | `/api/config` | Configuration |
| GET/POST | `/api/wifi` | WiFi status/config |
| GET | `/api/access/status` | Access control status |
| GET/POST | `/api/access/server` | Server config |
| POST | `/api/fingerprint/enroll` | Start fingerprint enrollment |
| GET | `/api/fingerprint/enroll/status` | Enrollment progress |
| POST | `/api/fingerprint/delete` | Delete fingerprint from sensor |
| GET | `/api/door` | Door state |
| POST | `/api/door/unlock` | Remote door unlock |

---

## Web UI Pages

### Dashboard
- Status cards (weight, baseline, contents, state)
- Quick actions (calibrate, add tool, refresh)
- Contents grid
- System info table

### Tools
- Tool list table
- Add/Edit/Delete modals
- Search/filter (future)

### Users
- Login form
- User list
- Add user modal

### Logs
- Event log table
- Export to CSV
- Pagination/limit selector

### Access Control
- Door status (locked/unlocked) and manual unlock
- Fingerprint enrollment (1-127 slots) with progress display
- Server configuration (URL, auth token, local fallback toggle)
- User → fingerprint ID mapping table
- Access log with decision badges (granted/denied/local_fallback)
- Server health indicator (online/offline/latency)

### Diagnostics
- Component status table
- Error details
- Restart button

### Config
- Sensor settings
- Power management
- Matching settings
- Factory reset

### WiFi
- SSID/Password form
- Network scanner
- Current connection status
- AP mode info

---

## Build & Upload

```bash
cd esp32_inventory_box

# Compile firmware
pio run

# Upload firmware
pio run --target upload

# Build SPIFFS
pio run --target buildfs

# Upload SPIFFS (web files)
pio run --target uploadfs

# Monitor serial
pio device monitor
```

---

## Fingerprint Access Control

### Overview
R307/AS608 fingerprint sensor + solenoid door lock. Central server validates access, ESP32 controls relay. Local fallback when server unreachable.

### How It Works
1. User places finger on R307 sensor
2. Sensor performs internal match → returns matched ID (1-127)
3. ESP32 sends ID + metadata to central server (`POST /api/access/check`)
4. Server responds `{allowed: true/false, userName, reason}`
5. If allowed → relay energizes, door unlocks for 5 seconds
6. If server unreachable → local User→fpId lookup (if fallback enabled)
7. Every event logged to SPIFFS + synced to server

### Server Protocol
```
POST /api/access/check
  {"deviceId":"inventory-box-01","fpId":7,"timestamp":N}
  → {"allowed":true,"userName":"John","userId":3,"reason":"authorized"}

GET /api/device/heartbeat?deviceId=X&uptime=Y&freeHeap=Z
  → {"status":"ok"}
```

### State Machine
```
IDLE → SCANNING → CHECKING_SERVER → GRANTED → UNLOCKING → UNLOCKED → LOCKED → IDLE
                                    → DENIED  → IDLE
                                    → (server down) → LOCAL_AUTH → GRANTED/DENIED
```

### Audit Trail
All access events logged with tags: `ACCESS`, `DOOR`, `FINGERPRINT`, `SERVER`. Stored in SPIFFS CSV with rotation. Batched sync to server every 20 events or 60 seconds.

### Hardware
| GPIO | Component | Notes |
|------|-----------|-------|
| 4 | Fingerprint TX (ESP→R307) | UART2 remapped |
| 5 | Fingerprint RX (R307→ESP) | UART2 remapped |
| 13 | Relay IN | Active LOW (safe-fail: GPIO float = locked) |
| 14 | Door reed switch | INPUT_PULLUP; LOW=closed, HIGH=open |

### NVS Config Keys
| Key | Type | Default |
|-----|------|---------|
| `cfg_server_url` | String | `""` |
| `cfg_server_token` | String | `""` |
| `cfg_access_local_fallback` | Bool | `true` |

## Error Handling

All init failures tracked by SystemStatus:
- Storage, HX711, MPU6050, Display, WiFi, WebServer, Fingerprint, Door, ServerClient, AccessController
- Errors shown in web UI banner + diagnostics page
- System continues with degraded functionality
- Restart available from web UI

---

## License

MIT