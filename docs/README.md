# ESP32 Inventory Box — Documentation

## Overview

Smart inventory tracking box using ESP32. Detects tool removal/placement via weight change + motion sensing. Tracks contents, logs events, serves web UI.

**Key Features:**
- Real-time weight monitoring (HX711 load cell)
- Motion detection (MPU6050 accelerometer)
- Auto-detect tool changes
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
│    diagnostics    2KB                           │
│    config         5KB                           │
│    wifi          7KB                           │
│  TOTAL          ~64KB                           │
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

### Presentation
| File | Purpose |
|------|---------|
| `presentation/WebServer.h/.cpp` | HTTP REST API, static file serving |

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

## Error Handling

All init failures tracked by SystemStatus:
- Storage, HX711, MPU6050, Display, WiFi, WebServer
- Errors shown in web UI banner + diagnostics page
- System continues with degraded functionality
- Restart available from web UI

---

## License

MIT