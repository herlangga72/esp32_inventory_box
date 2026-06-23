# ESP32 Inventory Box

Smart inventory tracking via weight (HX711) + motion (MPU6050) + fingerprint access (R307). ESP32, Arduino framework, PlatformIO.

## Commands
```bash
pio run                    # Build firmware
pio run --target upload    # Upload firmware
pio run --target buildfs   # Build SPIFFS (web assets)
pio run --target uploadfs  # Upload SPIFFS
pio device monitor         # Serial monitor (115200)
```

## Architecture — Microkernel Service Registry

Single global `ServiceRegistry g_registry` at fixed link-time address. All service state lives in compile-time-sized pools inside the registry struct. No heap allocation after boot. No STL. No virtual functions.

```
src/
├── config/          Pin defs, constants, task rates, operational modes
├── hal/             HX711, MPU6050, SSD1306, Fingerprint (R307), InterruptManager, RTC (DS3231)
├── domain/
│   ├── entities/    Tool, User, LogEntry, BoxState
│   ├── events/      EventBus (singleton pub/sub — legacy, barely used now)
│   └── services/    WeightService, MotionService, MatchingService,
│                    StateManager, AccessController, DoorService
├── data/            StorageManager (NVS wrapper), ToolRepo, UserRepo, LogRepo
├── kernel/          ServiceRegistry (microkernel), WiFiManager, PowerManager,
│                    SystemStatus, ServerClient, MailboxSystem (compat shim)
├── presentation/    WebServer (REST API + SPIFFS SPA), DisplayManager (OLED), SerialCLI
└── utils/           LogManager, LogFile, JsonBuilder, JsonParser
```

## ServiceRegistry — Core Microkernel

`ServiceRegistry.h` / `.cpp` — the heart of the system.

- **Single instance** `g_registry` at file scope, known link-time address.
- **22 services** in `ServiceId` enum. Each has a `ServiceControlBlock` (36 bytes) tracking inbox queue, message counts, heartbeat, task handle, owned memory offset.
- **Memory pools** — 5 fixed-size `uint8_t[]` pools inside the struct:
  - `kernelPool` (16 KB): Logger, Storage, WiFi, Power, SystemStatus
  - `halPool` (2 KB): HX711, MPU6050, SSD1306, Fingerprint driver scratch
  - `domainPool` (1 KB): WeightService, MotionService, StateManager, AccessController, DoorService
  - `dataPool` (8 KB): ToolRepo, UserRepo, LogRepo caches
  - `presentationPool` (1 KB): WebServer, DisplayManager, SerialCLI, ServerClient
- **Pool offsets** — compile-time `#define` macros (e.g., `SR_OFFSET_WEIGHT_SERVICE`). Typed accessors return `reinterpret_cast<T*>` into the pool.
- **Static queues** — `queueBufferPool` (4 KB) + `StaticQueue_t` array. `registerMailbox(ServiceId, depth)` allocates from these pools via `xQueueCreateStatic`.

### ServiceMessage — 16-byte IPC
```cpp
struct ServiceMessage {
    uint8_t target;    // ServiceId
    uint8_t type;      // message-type enum
    uint8_t replyTo;   // for request-reply patterns
    uint8_t corrId;    // correlation ID
    union {            // 12-byte payload — f2, f1u2, u32, u2, u4, bytes, raw[12] };
};
```
Messages sent via `g_registry.send(target, msg)` → FreeRTOS queue. Each service drains its inbox in its task loop. `sendCmd(id, type)` for fire-and-forget. `query(target, request, reply, timeout)` for request-reply.

### Per-Service Message Types
| Enum | Used By | Key Values |
|------|---------|------------|
| `KernelMsgType` | PowerManager | PING, ACTIVITY, MOTION_WAKE, ENTER_SLEEP |
| `StateMsgType` | StateManager | WEIGHT_CHANGE, MOTION_DETECTED, TOOL_MATCHED, USER_LOGIN/LOGOUT, CALIBRATION |
| `WeightMsgType` | WeightService | SET_BASELINE, START_CALIBRATION, TARE, QUERY_WEIGHT |
| `AccessMsgType` | AccessController | BEGIN_ENROLLMENT, CANCEL_ENROLLMENT, DELETE_FINGERPRINT, REMOTE_UNLOCK |
| `DoorMsgType` | DoorService | UNLOCK, LOCK, SET_DURATION |
| `DisplayMsgType` | DisplayManager | STATE_CHANGED, TOOL_PLACED/REMOVED, WEIGHT_UPDATE, NOTIFICATION, SLEEP/WAKE |

### Domain Services Pattern
All domain services use **free functions on typed memory structs** — no classes, no virtual dispatch:

```cpp
// WeightServiceMemory lives at SR_OFFSET_WEIGHT_SERVICE in domainPool
void ws_onRawReading(WeightServiceMemory* mem, int32_t raw);
void ws_update(WeightServiceMemory* mem);
float ws_getCurrentWeight(const WeightServiceMemory* mem);

// StateManagerMemory — same pattern
void sm_init(StateManagerMemory* mem, StorageManager* storage);
void sm_dispatchMessage(StateManagerMemory* mem, const ServiceMessage& msg);
void sm_updatePeriodic(StateManagerMemory* mem);

// AccessControllerMemory
void ac_init(AccessControllerMemory* mem);
void ac_dispatchMessage(AccessControllerMemory* mem, const ServiceMessage& msg,
                        FingerprintDriver* fp, ServerClient* sc, DoorServiceMemory* ds);
void ac_update(AccessControllerMemory* mem, FingerprintDriver* fp,
               ServerClient* sc, DoorServiceMemory* ds);
```

Callers get typed pointers from `g_registry.getWeightService()`, etc.

### MailboxSystem — Backward-Compat Shim
`MailboxSystem.h` is a singleton wrapper that delegates all calls to `g_registry`. Legacy code uses `MailboxSystem::getInstance().send(...)`. New code uses `g_registry.send(...)` directly. No functional difference.

### SystemStatus — Free Functions on Registry Memory
`ss_begin(mem)`, `ss_markOK(mem, "HX711")`, `ss_markError(mem, "WiFi", "reason")`, etc. Component tracking with OK/WARNING/ERROR states. Boot stage enum: `BS_STORAGE → BS_HX711 → BS_MPU6050 → BS_DISPLAY → BS_WIFI → BS_WEB_SERVER → BS_FINGERPRINT → BS_ACCESS_SERVER → BS_COMPLETE`. Also provides inline wrappers like `systemStatus_hasErrors()` that auto-resolve from `g_registry`.

## FreeRTOS Tasks

| Task | Priority | Core | AP Rate | STA Rate | Conditional? |
|------|----------|------|---------|----------|-------------|
| State | 10 | 0 | 20 Hz | 5 Hz | No |
| Access | 9 | 0 | 5 Hz | 2 Hz | Fingerprint OK |
| Weight | 8 | 0 | 10 Hz | 2 Hz | HX711 OK |
| Motion | 8 | 0 | 100 Hz | 10 Hz | MPU6050 OK |
| WiFi | 6 | 0 | 100 Hz | 10 Hz | No |
| Web | 5 | 0 | 1 kHz | 20 Hz | WebServer OK |
| Display | 3 | 1 | 1 Hz | 0.2 Hz | Display OK |

Task rates are **dynamic** — `opDelay(AP_MS, STA_MS)` reads `OperationalMode` from registry. AP mode = full speed. STA mode = power-saving slower rates.

## Boot Sequence (main.cpp)

1. **LED blink** (3 fast pulses) → 2s delay
2. **BOOT button check** — if held, wipe WiFi creds (force AP mode)
3. **Serial + Logger init**
4. **`g_registry.init()`** — zero all pools, set magic/version/boot-count, init SCBs
5. **`ss_begin()`** — init SystemStatus, pre-register 10 components
6. **`initWithRetry()`** for each hardware component:
   - Storage (NVS, 0 retries — must work)
   - HX711 (3 retries, 2s delay)
   - RTC/DS3231 (3 retries, 1s delay) — I2C_NUM_1 pins 18/23. Fallback: compile-time + uptime
   - MPU6050 (3 retries, 1.5s delay)
   - Display/SSD1306 (2 retries, 2s delay)
   - Fingerprint/R307 (3 retries, 2s delay)
7. **Interrupt config** — disable interrupts for failed sensors
8. **Mailbox registration** — `registerMailbox()` for 7 services
9. **Service init** — WeightService baseline load, MotionService accel capture, StateManager init, repo init
10. **WiFi** — STA connect with stored creds → AP fallback
11. **WebServer** (STA only, skipped in AP mode)
12. **DoorService, ServerClient, AccessController, DisplayManager** init
13. **mDNS + OTA** (debug builds only)
14. **Task creation** — conditional: skip tasks for failed components
15. **`ss_setBootComplete()`**

### Graceful Degradation
`initWithRetry()` retries with backoff. On final failure: marks component ERROR in SystemStatus, sensor interrupt disabled, dependent task not created. System runs with reduced functionality.

## WiFi Manager
- **STA mode**: connects with stored creds. 15s timeout. Non-blocking reconnect state machine — polls every 5s, reconnects on drop, falls back to AP after 10s fail.
- **AP mode**: SSID `Inventory-Box-Setup`, channel 6, 20MHz b/g/n, 19.5dBm TX. Captive portal at 192.168.4.1. Serves `wifi-setup.html` from SPIFFS. POST to `/save` → store creds → reboot.
- **Config**: `WiFi.setSleep(false)` — keeps radio on, prevents modem-sleep disconnects.
- **Credentials**: stored in NVS via `storage.putString("wifi_ssid"/"wifi_pass", ...)`.

## Access Control Subsystem

4 components working together:

- **FingerprintDriver** (`hal/`) — wraps Adafruit Fingerprint library over HardwareSerial (UART2, remapped RX=5/TX=4, 57600 baud). Non-blocking poll model: `startScan()` → `checkScan()`. Multi-step enrollment: `startEnroll(id)` → `checkEnrollStep()` → 0 (need image1) → 1 (need image2) → 2 (success).

- **AccessController** (`domain/services/`) — state machine: IDLE → SCANNING → CHECKING_SERVER → LOCAL_AUTH_CHECK → GRANTED/DENIED → UNLOCKING → UNLOCKED → LOCKING. Supports server-first auth with local fallback. Enrollment state machine for fingerprint registration.

- **DoorService** (`domain/services/`) — relay control (pin 13, active LOW, 5s pulse). Reed switch monitoring (pin 14, INPUT_PULLUP). States: LOCKED → UNLOCKING → UNLOCKED → HELD_OPEN.

- **ServerClient** (`kernel/`) — HTTP client to remote access server. `checkAccess(fpId)` returns -1/0/1. Heartbeat health checks. Access log batch sync. Connection stats (response time, fail duration). Config stored in NVS (`cfg_server_url`, `cfg_server_token`).

## Pin Map

| Pin | Function | Notes |
|-----|----------|-------|
| 16 | HX711 DT | Load cell data |
| 17 | HX711 SCK | Load cell clock |
| 36 | HX711 DRDY | Data ready interrupt (input only) |
| 21 | I2C SDA | Shared: MPU6050 + SSD1306 |
| 22 | I2C SCL | Shared: MPU6050 + SSD1306 |
| 35 | MPU INT | Motion interrupt |
| 19 | OLED RST | Display reset |
| 33 | Button | BOOT — hold during power-on to force AP mode |
| 2  | LED | Built-in blue, active LOW |
| 5  | FP RX | Fingerprint sensor UART RX |
| 4  | FP TX | Fingerprint sensor UART TX |
| 13 | Relay | Solenoid door lock, active LOW |
| 14 | Door Sensor | Reed switch, INPUT_PULLUP, LOW=closed |
| 18 | RTC SDA | DS3231 I2C (I2C_NUM_1, separate bus) |
| 23 | RTC SCL | DS3231 I2C (I2C_NUM_1, separate bus) |

## Web API

### Core
- `GET /api/status` — weight, baseline, delta, state, contents, current user, WiFi info, system status, free heap
- `GET/POST /api/tools`, `GET/PUT/DELETE /api/tools/{id}` — CRUD
- `GET/POST /api/users`, `POST /api/users/login`, `POST /api/users/logout`, `DELETE /api/users/{id}`
- `GET /api/logs?limit=&offset=`, `GET /api/logs/download` (CSV), `POST /api/logs/clear`
- `POST /api/calibrate` — save current baseline
- `GET /api/diagnostics` — per-component status with error counts
- `POST /api/restart`
- `GET/POST /api/wifi`, `GET /scan` (WiFi site survey)
- `GET/POST /api/config` — runtime config (thresholds, sleep timeouts, log level, factory reset)

### Access Control
- `GET /api/access/status` — access state, last event, local fallback status, server health
- `GET/POST /api/access/server` — server URL, auth token, local fallback toggle
- `POST /api/fingerprint/enroll`, `GET /api/fingerprint/enroll/status` — enrollment lifecycle
- `POST /api/fingerprint/delete` — delete by fpId
- `GET /api/door`, `POST /api/door/unlock` — door state + remote unlock

### SPIFFS SPA
`data/` → SPIFFS. `index.html` = shell, `pages/*.html` loaded on-demand (dashboard, tools, users, logs, diagnostics, config, wifi). `styles.css` + `app.js`. All routes serve from SPIFFS with `spiffsLock()`/`spiffsUnlock()` for thread safety.

## Data Layer

### StorageManager
NVS wrapper. `begin()`, `putString/getString`, `putFloat/getFloat`, `putInt/getInt`, `remove()`, `clear()`. Namespace: `"inventory_box"`. Uses ESP32 Preferences library internally (heap-allocates — unavoidable).

### Repositories — Free Functions on Pool Memory
All repos operate on `*Memory` structs in registry data pool:

```cpp
// ToolRepository — ToolRepositoryMemory (cache[20] + count + cacheValid)
void tr_init(ToolRepositoryMemory* mem, StorageManager* storage);
Tool* tr_findById(ToolRepositoryMemory* mem, StorageManager* storage, int id);
int tr_findAll(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int max);

// UserRepository — UserRepositoryMemory (cache[50] + count + cacheValid)
User* ur_authenticate(UserRepositoryMemory* mem, StorageManager* storage, const char* pin);
User* ur_findByFingerprintId(UserRepositoryMemory* mem, StorageManager* storage, int fpId);

// LogRepository — LogRepositoryMemory (SPIFFS-backed, circular buffer 500 max)
```

Serialization: pipe-delimited strings in NVS (`"id|name|weight|tolerance|active"`). Entities have `serialize()` / `deserialize()` methods.

## Power Management
- **Light sleep**: `lightSleepThreshold` ms idle → auto light sleep. Wake from MPU interrupt or timer.
- **Deep sleep**: `deepSleepThreshold` ms idle → deep sleep. Wake from timer only (MPU interrupt doesn't survive deep sleep).
- **Activity tracking**: WeightService and MotionService send `ACTIVITY` / `MOTION_WAKE` kernel messages to PowerManager's mailbox. `loop()` drains PowerManager inbox.
- **Configurable**: thresholds settable via `/api/config`. Baseline stored for weight-change wake detection.
- **Cold boot**: restores baseline from RTC memory, reinitializes wake sources if MPU available.

## Operational Modes
`OperationalMode` enum drives task rates and power behavior:
- `OP_AP_FULL` — AP active, full-speed task rates, no power saving
- `OP_STA_IDLE` — STA connected, slow task rates, power saving enabled

Set during WiFi init, propagated to both SystemStatus and PowerManager. Tasks read mode via `ss_getOperationalMode()`.

## Key Patterns
- **No heap after boot**: all service state in compile-time pools. Only unavoidable heap: Preferences (NVS internals), WebServer (Arduino library), SPIFFS File objects.
- **No virtual functions**: free functions + structs. No class hierarchies outside Arduino/Adafruit libraries.
- **Static queues only**: `xQueueCreateStatic` with pool-allocated buffers. No `xQueueCreate` (heap).
- **ISR-safe messaging**: `g_registry.sendFromISR()` for interrupt → task communication.
- **Message-driven state**: services communicate via mailbox messages, not direct function calls or event bus. StateManager is primary hub for domain events.
- **Conditional task creation**: each task gated on component status. Failed sensor = no task = no wasted CPU.
- **Dynamic timing**: `opDelay(apMs, staMs)` reads current mode. Single code path, two timing profiles.
- **LED signaling**: AP mode = solid on, connected = slow blink (3s), errors = fast blink (200ms), disconnected = medium blink (500ms).

## RTC (DS3231)
- `rtc_init()` probes DS3231 on I2C_NUM_1 (pins 18/23, addr 0x68), calls `settimeofday()` on success
- Fallback: `rtc_setFallbackTime()` sets clock to `__DATE__` + `__TIME__`, uptime from there
- `time(nullptr)` works in all code paths. `/api/diagnostics` shows "RTC" component status.
