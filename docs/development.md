# Development Guide

## Build Commands

```bash
pio run                          # Build firmware
pio run --target upload          # Build + flash to ESP32
pio run --target buildfs         # Build SPIFFS image (from data/)
pio run --target uploadfs        # Upload SPIFFS to ESP32
pio device monitor               # Serial monitor (115200 baud)
pio run --target clean           # Clean build artifacts
```

**Monitor filters** (in `platformio.ini`): `colorize`, `time`, `send_on_enter`

## Project Structure

```
esp32_inventory_box/
├── platformio.ini           # PlatformIO config (board, framework, libs, FreeRTOS)
├── data/                    # SPIFFS web UI assets
│   ├── index.html           # SPA shell
│   ├── styles.css
│   ├── app.js
│   └── pages/               # Loaded on-demand by SPA
│       ├── dashboard.html
│       ├── tools.html
│       ├── users.html
│       ├── logs.html
│       ├── diagnostics.html
│       ├── config.html
│       └── wifi.html
├── src/
│   ├── main.cpp             # Boot sequence, task creation, loop()
│   ├── config/
│   │   └── Config.h         # Pins, constants, task rates, operational modes
│   ├── hal/                 # Hardware abstraction layer
│   │   ├── HX711Driver.h/.cpp
│   │   ├── MPU6050Driver.h/.cpp
│   │   ├── SSD1306Driver.h/.cpp
│   │   ├── FingerprintDriver.h/.cpp
│   │   ├── RtcDriver.h/.cpp
│   │   └── InterruptManager.h/.cpp
│   ├── domain/
│   │   ├── entities/        # Data structs: Tool, User, LogEntry, BoxState
│   │   ├── events/          # EventBus (legacy pub/sub) + Events.h
│   │   └── services/        # Domain logic (free functions on *Memory)
│   │       ├── WeightService.h/.cpp
│   │       ├── MotionService.h/.cpp
│   │       ├── MatchingService.h/.cpp
│   │       ├── StateManager.h/.cpp
│   │       ├── AccessController.h/.cpp
│   │       └── DoorService.h/.cpp
│   ├── data/                # Persistence layer
│   │   ├── StorageManager.h/.cpp    # NVS wrapper
│   │   ├── ToolRepository.h/.cpp    # Free functions on ToolRepositoryMemory
│   │   ├── UserRepository.h/.cpp    # Free functions on UserRepositoryMemory
│   │   └── LogRepository.h/.cpp     # SPIFFS-backed circular buffer
│   ├── kernel/              # System services
│   │   ├── ServiceRegistry.h/.cpp   # Microkernel — global singleton
│   │   ├── MailboxSystem.h/.cpp     # Backward-compat shim
│   │   ├── SystemStatus.h/.cpp      # Component health tracking
│   │   ├── WiFiManager.h/.cpp       # STA + AP fallback
│   │   ├── PowerManager.h/.cpp      # Light/deep sleep
│   │   └── ServerClient.h/.cpp      # Remote access server HTTP client
│   ├── presentation/        # UI + API
│   │   ├── WebServer.h/.cpp         # REST API + SPIFFS SPA
│   │   ├── DisplayManager.h/.cpp    # OLED screen manager
│   │   └── SerialCLI.h/.cpp         # Serial command-line interface
│   └── utils/
│       ├── LogManager.h/.cpp        # Async logger (FreeRTOS queue-based)
│       ├── LogFile.h/.cpp           # SPIFFS log file operations
│       ├── JsonBuilder.h/.cpp       # Minimal JSON serializer (no heap)
│       └── JsonParser.h/.cpp        # Minimal JSON parser (fixed fields)
└── docs/                    # Documentation
```

## Code Patterns

### 1. Free Functions on Pool Memory

All domain services, repositories, and system status use this pattern:

```cpp
// Header — declare free function
void ws_update(WeightServiceMemory* mem);

// Caller — get typed pointer from registry
auto* wm = g_registry.getWeightService();
ws_update(wm);

// Implementation — operate directly on memory struct
void ws_update(WeightServiceMemory* mem) {
    ServiceMessage msg;
    while (g_registry.tryReceive(ServiceId::WEIGHT_SERVICE, msg)) {
        // handle message
    }
}
```

**Never** create service classes with internal state. State lives in registry pool. Functions are stateless.

### 2. Adding a New Service

1. Define `ServiceId` enum value in `ServiceRegistry.h`
2. Define memory struct with `static_assert` on size
3. Add pool offset macro
4. Add `static_assert` for pool capacity
5. Add typed accessor method declaration + implementation
6. Create service header with free functions
7. Register mailbox in `main.cpp` setup
8. Create service implementation

### 3. Adding a New Message Type

1. Add value to appropriate enum (or create new enum) in `ServiceRegistry.h`
2. Sender: create `ServiceMessage::cmd(target, type)` + set payload fields
3. Receiver: add case to `switch(msg.type)` in dispatch function

### 4. Thread Safety

- **Single writer per mailbox**: each service's task is the only reader of its inbox
- **ISR → task**: use `g_registry.sendFromISR()` with `pxHigherPriorityTaskWoken`
- **SPIFFS**: `spiffsLock()`/`spiffsUnlock()` around all SPIFFS operations
- **No mutexes needed**: unicore for domain tasks (Core 0), Display alone on Core 1
- **NVS**: ESP32 Preferences is thread-safe for get/put (but not iterator)

### 5. Memory Allocation Rules

- **No `new`/`malloc` after boot**: all service state in compile-time pools
- **Unavoidable heap**: Preferences (NVS internals), WebServer (Arduino library), SPIFFS File objects, `std::function` (EventBus lambdas), `std::vector` (LogRepository returns)
- **Static queues only**: `xQueueCreateStatic` with pool-allocated buffers
- **Stack monitoring**: each task has fixed stack. Watch for stack overflow via `uxTaskGetStackHighWaterMark()`

### 6. Error Handling

- **Graceful degradation**: `initWithRetry()` + conditional task creation
- **No exceptions**: Arduino framework doesn't support them
- **Return values**: bool for success/fail, INT32_MIN for HX711 errors, -1 for fingerprint no-read
- **SystemStatus**: component-level error tracking with messages
- **Logging**: LOG_ERROR/WARN/INFO/DEBUG macros everywhere

### 7. JSON Parsing (Fixed-Field)

Minimal JSON parser for fixed schemas. No heap allocation:

```cpp
struct CreateToolReq { char name[32]; float weight; float tolerance; };
CreateToolReq req; memset(&req, 0, sizeof(req)); req.tolerance = Config::DEFAULT_TOLERANCE;

JField fields[] = {
    {"name",      JField::T_STR, req.name, sizeof(req.name)},
    {"weight",    JField::T_FLT, &req.weight},
    {"tolerance", JField::T_FLT, &req.tolerance},
};
String err;
if (!jsonParse(body, fields, 3, err)) {
    sendError(400, "Invalid JSON");
    return;
}
```

### 8. JSON Building (No-Heap)

```cpp
JsonBuilder jb;
jb.startObj();
jb.addStr("name", "Hammer");
jb.addInt("id", 1);
jb.addFlt("weight", 450.0);
jb.addBool("active", true);
jb.startArr("items");
jb.startArrObj();
jb.addStr("key", "val");
jb.endObj();
jb.endArr();
jb.endObj();
sendJson(jb.str());  // String on stack
```

## Debugging

### Serial Monitor
```bash
pio device monitor
```

Log output format: `[LEVEL] [TAG] message`

### Serial CLI (debug builds only)
Commands available at 115200 baud:
- `help` — show commands
- `status` — uptime, heap, CPU, WiFi RSSI, counts
- `tools` — list all tools
- `users` — list all users
- `logs` — recent log entries
- `wifi` — WiFi status
- `mem` — heap stats
- `log level <none|error|warn|info|debug>` — change log level
- `reboot` — restart

### OTA Updates (debug builds only)
```bash
pio run --target upload  # Over WiFi after initial flash
# Or: use ArduinoOTA in IDE
```
Hostname: `inventory-box`. mDNS: `http://inventory-box.local`

### Heap Tracking
```cpp
LOG_INFO("TAG", "Free heap: %d", ESP.getFreeHeap());
// Check stack high water mark:
UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);  // current task
```

### Registry Debug
```cpp
ServiceControlBlock* scb = g_registry.getSCB(ServiceId::STATE_MANAGER);
LOG_INFO("DBG", "SCB[%d]: state=%d msgs_recv=%d msgs_drop=%d heartbeat=%d",
    scb->id, scb->state, scb->msg_received, scb->msg_dropped, scb->last_heartbeat_ms);
```

## PlatformIO Config (`platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
monitor_filters = colorize, time, send_on_enter

board_build.partitions = default_spiffs.csv
board_build.flash_mode = dio
board_build.f_cpu = 240000000L

build_flags =
    -DCORE_DEBUG_LEVEL=0         # No debug logging overhead
    -DCONFIG_ARDUINO_LOOP_STACK_SIZE=4096
    -DCONFIG_FREERTOS_UNICORE     # Core 0 only (+ Core 1 for Display)
    -DCONFIG_FREERTOS_HZ=1000     # 1kHz tick
    -DCONFIG_FREERTOS_USE_TICKLESS_IDLE=1

lib_deps =
    adafruit/Adafruit GFX Library @ ^1.11.9
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit Fingerprint Sensor Library @ ^2.1.2
```

## RELEASE_BUILD Flag

Code wrapped in `#ifndef RELEASE_BUILD`:
- SerialCLI (debug serial commands)
- ArduinoOTA (over-the-air updates)
- mDNS (Bonjour service discovery)
- `ESPmDNS.h` includes

To enable release build: add `-DRELEASE_BUILD` to `build_flags`.

## Boot-Time Safety

- **2-second delay**: after LED blink, before Serial init — allows time to connect serial monitor
- **BOOT button check**: hold button during power-on to force AP mode (wipes creds)
- **WDT**: `yield()` calls in long operations (STA connect, init retries) feed watchdog
- **Component status summary**: always logged before task creation — grep for `COMPONENT STATUS`

## Common Modifications

### Change weight threshold
Set `Config::WEIGHT_THRESHOLD_GRAMS` (default 2.0g) or POST to `/api/config` with `threshold`.

### Change settling time
Set `Config::SETTLING_TIME_MS` (default 3000ms) or POST to `/api/config` with `settlingTime`.

### Add new API endpoint
1. Register route in `WebServerManager::begin()` (in `WebServer.cpp`)
2. Implement handler method
3. Call registry accessors for data (`_rst()`, `_rwt()`, etc.)

### Add new SPIFFS page
1. Create page HTML in `data/pages/`
2. Add route in `WebServerManager::begin()`
3. Run `pio run --target buildfs && pio run --target uploadfs`

### Change task rate
Modify `TaskRate::AP_*_MS` or `TaskRate::STA_*_MS` in `Config.h`. No recompile of service files needed.

## Testing

No formal test framework (ESP32 Arduino has no native unit testing). Manual testing approach:

1. **Boot test**: power on, check serial for component status — all should be OK
2. **RTC test**: check serial for `RTC: OK`; `/api/diagnostics` shows RTC status. Disconnect RTC → verify fallback message.
3. **Degraded test**: disconnect HX711, boot — verify HX711 shows ERROR, weight task not created
4. **WiFi test**: boot without creds → should enter AP mode. Connect, set creds via portal → should reboot in STA
5. **Weight test**: place known weight → verify delta and matching in `/api/status`
6. **Access test**: enroll fingerprint, scan → verify door unlock and USER_LOGIN event
7. **API test**: curl each endpoint, verify response schema
