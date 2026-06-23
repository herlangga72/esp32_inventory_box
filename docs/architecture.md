# Architecture — Microkernel Service Registry

## Overview

Single global `ServiceRegistry g_registry` at fixed link-time address. Every service's state lives at a known compile-time offset from `&g_registry`. No heap allocation after boot. No STL containers. No virtual functions.

```
┌─────────────────────────────────────────────────────────┐
│                    ServiceRegistry                       │
│  ┌───────────────────────────────────────────────────┐  │
│  │ Header (16B): magic, version, serviceCount,        │  │
│  │              checksum, uptimeMs, bootCount          │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ SCB[22] (792B): ServiceControlBlock per service    │  │
│  │   id | state | flags | inbox | msg counts |        │  │
│  │   heartbeat | task | memory_offset | memory_size   │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ kernelPool (16 KB)                                 │  │
│  │   LoggerMemory | StorageMemory | WiFiMemory        │  │
│  │   PowerMemory | SystemStatusMemory                 │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ halPool (2 KB)                                     │  │
│  │   HX711(64B) | MPU6050(64B) | SSD1306(128B)       │  │
│  │   Fingerprint(128B) | RTC (stack vars, no pool)    │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ domainPool (1 KB)                                  │  │
│  │   WeightService | MotionService | StateManager     │  │
│  │   AccessController | DoorService                   │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ dataPool (8 KB)                                    │  │
│  │   ToolRepository | UserRepository | LogRepository  │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ presentationPool (1 KB)                            │  │
│  │   WebServer | DisplayManager | SerialCLI           │  │
│  │   ServerClient                                     │  │
│  ├───────────────────────────────────────────────────┤  │
│  │ queueBufferPool (4 KB) + StaticQueue_t[22]         │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

Total struct size: ~33 KB. All at one link-time address.

## ServiceId Enum (22 services)

```
NONE=0              KERNEL (1-5):     DOMAIN (6-11):
LOGGER=1             STORAGE=2         STATE_MANAGER=6
WIFI=3               POWER=4           WEIGHT_SERVICE=7
SYSTEM_STATUS=5                        MOTION_SERVICE=8
                                       ACCESS_CONTROLLER=9
DATA (12-14):                          DOOR_SERVICE=10
TOOL_REPOSITORY=12                     MATCHING_SERVICE=11
USER_REPOSITORY=13
LOG_REPOSITORY=14   PRESENTATION(15-17):  HAL(19-22):
                    WEB_SERVER=15          HX711=19
                    DISPLAY_MANAGER=16     MPU6050=20
                    SERIAL_CLI=17          SSD1306=21
                    SERVER_CLIENT=18       FINGERPRINT=22
```

## ServiceControlBlock (36 bytes)

```cpp
struct ServiceControlBlock {
    uint8_t  id;                  // ServiceId value
    uint8_t  state;               // ServiceState: UNINIT→INIT→RUNNING/ERROR/STOPPED
    uint16_t flags;               // HAS_INBOX | HAS_TASK | IS_PASSIVE | HEARTBEAT
    QueueHandle_t inbox;          // FreeRTOS queue (static allocation)
    uint32_t msg_received;        // monotonic counters
    uint32_t msg_sent;
    uint32_t msg_dropped;
    uint32_t last_heartbeat_ms;   // for watchdog/liveness
    TaskHandle_t task;            // if this service owns a task
    uint32_t owned_memory_offset; // offset into pool
    uint16_t owned_memory_size;
    uint16_t reserved;
};
```

## Mailbox IPC — ServiceMessage (16 bytes)

All inter-service communication goes through FreeRTOS queues. Direct function calls between services are avoided except for read-only queries.

```cpp
struct ServiceMessage {
    uint8_t target;    // ServiceId destination
    uint8_t type;      // message-type enum value
    uint8_t replyTo;   // ServiceId for request-reply correlation
    uint8_t corrId;    // correlation ID
    union {
        struct { float f1; float f2; }                f2;    // 2 floats
        struct { float f1; uint16_t u1; uint16_t u2; } f1u2;  // float + 2 u16
        struct { uint32_t u32_1; uint32_t u32_2; }     u32;   // 2 u32
        struct { uint16_t u1, u2, u3, u4; }           u4;    // 4 u16
        struct { uint8_t b0..b11; }                   bytes; // 12 bytes
        uint8_t raw[12];                                      // raw access
    };
};
```

### Sending Messages

```cpp
// Fire-and-forget
g_registry.sendCmd(ServiceId::POWER, (uint8_t)KernelMsgType::ACTIVITY);

// With payload
ServiceMessage msg = ServiceMessage::cmd(ServiceId::STATE_MANAGER,
    (uint8_t)StateMsgType::WEIGHT_CHANGE);
msg.f2.f1 = delta;
msg.f2.f2 = currentWeight;
g_registry.send(ServiceId::STATE_MANAGER, msg);

// From ISR (e.g., interrupt handler)
BaseType_t woken = pdFALSE;
g_registry.sendFromISR(ServiceId::STATE_MANAGER, msg, &woken);
if (woken) portYIELD_FROM_ISR();

// Request-reply
ServiceMessage req = ServiceMessage::query(target, replyTo, msgType, corrId);
ServiceMessage reply;
g_registry.query(target, req, reply, pdMS_TO_TICKS(500));
```

### Receiving Messages

```cpp
// Blocking with timeout
ServiceMessage msg;
g_registry.receive(ServiceId::STATE_MANAGER, msg, pdMS_TO_TICKS(50));

// Non-blocking (drain)
while (g_registry.tryReceive(ServiceId::STATE_MANAGER, msg)) {
    sm_dispatchMessage(mem, msg);
}
```

## Per-Service Message Types

| Enum | Scope | Values |
|------|-------|--------|
| `KernelMsgType` | PowerManager, system | PING=0, ACTIVITY=1, MOTION_WAKE=2, ENTER_SLEEP=3, SET_BASELINE=4, SET_OPERATIONAL_MODE=5, STORAGE_GET/PUT/REMOVE=10-12, WIFI_STATUS_QUERY=20, STATUS_QUERY=21 |
| `StateMsgType` | StateManager | WEIGHT_CHANGE=1, MOTION_DETECTED=2, TOOL_MATCHED=3, UNKNOWN_WEIGHT=4, USER_LOGIN=5, USER_LOGOUT=6, CALIBRATION=7, ENTER_SLEEP=8, WAKE=9 |
| `WeightMsgType` | WeightService | SET_BASELINE=1, START_CALIBRATION=2, TARE=3, QUERY_WEIGHT=10, QUERY_BASELINE=11 |
| `MotionMsgType` | MotionService | QUERY_MOTION_STATE=1, QUERY_ACCEL=2 |
| `AccessMsgType` | AccessController | BEGIN_ENROLLMENT=1, CANCEL_ENROLLMENT=2, DELETE_FINGERPRINT=3, DELETE_ALL_FP=4, REMOTE_UNLOCK=5 |
| `DoorMsgType` | DoorService | UNLOCK=1, LOCK=2, SET_DURATION=3 |
| `DisplayMsgType` | DisplayManager | STATE_CHANGED=1, TOOL_PLACED=2, TOOL_REMOVED=3, WEIGHT_UPDATE=4, NOTIFICATION=5, USER_LOGIN=6, USER_LOGOUT=7, SLEEP=8, WAKE=9 |
| `RepoMsgType` | Repositories | FIND_ALL=1, FIND_BY_ID=2, CREATE=3, UPDATE=4, REMOVE=5, FIND_BY_FP_ID=6, COUNT=7 |

## Message Flow Diagram

```
┌──────────┐  raw reading   ┌──────────────┐  WEIGHT_CHANGE   ┌──────────────┐
│ HX711    │ ──────────────→│ WeightService │ ────────────────→│ StateManager │
│ (ISR)    │                │ (mailbox)     │                  │ (mailbox)    │
└──────────┘                └──────────────┘                  └──────┬───────┘
                                                                    │
┌──────────┐  accel data    ┌──────────────┐  MOTION_DETECTED       │
│ MPU6050  │ ──────────────→│ MotionService │ ──────────────────────┘
│ (ISR)    │                │               │
└──────────┘                └──────────────┘

┌──────────┐  fp scan       ┌─────────────────┐  UNLOCK cmd
│ R307 FP  │ ──────────────→│ AccessController │ ──────────────┐
│ (poll)   │                │ (mailbox)        │               │
└──────────┘                └────────┬────────┘               │
                                     │                        ↓
                          ┌──────────↓──────────┐    ┌──────────────┐
                          │ ServerClient (HTTP) │    │ DoorService  │
                          └─────────────────────┘    │ (mailbox)    │
                                                     └──────────────┘

┌─────────────┐  USER_LOGIN
│ WebServer   │ ──────────────→ StateManager
│ (HTTP API)  │
└─────────────┘

                          ┌──────────────┐  STATE_CHANGED
                          │ StateManager │ ──────────────→┌──────────────┐
                          └──────────────┘                │ Display      │
                                                          │ Manager      │
┌──────────┐  ACTIVITY     ┌──────────────┐               └──────────────┘
│ Any task │ ─────────────→│ PowerManager │
└──────────┘               │ (mailbox)    │
                           └──────────────┘

                     ┌──────────┐
                     │ EventBus │ (legacy — published alongside
                     └──────────┘  mailbox for backward compat)
```

## Pool Allocation Pattern

Each service's memory struct is accessed via typed accessor:

```cpp
// ServiceRegistry method (inlined, no call overhead)
WeightServiceMemory* ServiceRegistry::getWeightService() {
    return reinterpret_cast<WeightServiceMemory*>(
        &domainPool[SR_OFFSET_WEIGHT_SERVICE - SR_OFFSET_DOMAIN_POOL]
    );
}

// Caller
auto* wm = g_registry.getWeightService();
ws_update(wm);
```

Offsets are compile-time `#define` macros from `ServiceRegistry.h`:
```
SR_OFFSET_WEIGHT_SERVICE  = domainPool + 0
SR_OFFSET_MOTION_SERVICE  = domainPool + sizeof(WeightServiceMemory)
SR_OFFSET_STATE_MANAGER   = domainPool + sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory)
...
```

Pool capacities verified at compile-time via `static_assert`:
```cpp
static_assert(SR_POOL_DOMAIN_SIZE >=
    sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory) +
    sizeof(StateManagerMemory) + sizeof(AccessControllerMemory) +
    sizeof(DoorServiceMemory),
    "Domain pool too small");
```

## SystemStatus

Free functions operating on `SystemStatusMemory*` in kernel pool:

```cpp
// Mark component status during boot
ss_begin(mem);
ss_markOK(mem, "HX711");
ss_markWarning(mem, "ServerClient", "No server URL configured");
ss_markError(mem, "WiFi", "Connection failed");

// Query
ComponentStatus st = ss_getStatus(mem, "MPU6050");  // OK, WARNING, ERROR, UNKNOWN
ComponentStatus overall = ss_getOverallStatus(mem);
bool hasErrors = ss_hasErrors(mem);
const char* lastErr = ss_getLastError(mem);

// Boot stage tracking
ss_setBootStage(mem, BootStage::BS_HX711);
ss_setBootComplete(mem);
```

10 components tracked: Storage, HX711, MPU6050, Display, WiFi, WebServer, Fingerprint, Door, ServerClient, AccessController, RTC (11 total).

Inline wrappers available: `systemStatus_hasErrors()`, `systemStatus_getOverallStatus()`, etc. (auto-resolve `g_registry.getSystemStatus()`).

## MailboxSystem — Backward-Compat Shim

`MailboxSystem.h` is a singleton wrapper that delegates to `g_registry`:

```cpp
class MailboxSystem {
public:
    static MailboxSystem& getInstance() { ... }
    bool send(ServiceId id, const ServiceMessage& msg) { return g_registry.send(id, msg); }
    bool receive(ServiceId id, ServiceMessage& msg, TickType_t t) { return g_registry.receive(id, msg, t); }
    // ...
};
```

Legacy code in `WebServer.cpp` still uses `MailboxSystem::getInstance().send(...)`. New code uses `g_registry.send(...)` directly. Equivalent.

## EventBus — Legacy Pub/Sub

`EventBus` (`domain/events/`) is a singleton using `std::map<DomainEvent, std::vector<std::function>>`. Still exists for backward compatibility — domain services publish to both EventBus AND mailbox simultaneously. Only DisplayManager subscribes to EventBus (for weight updates, tool placed/removed). Not used for core data flow.

32 event types in `DomainEvent` enum covering weight, motion, tool, user, access, door, fingerprint, server, and system events.

## Layer Dependencies

```
presentation/ ──→ domain/services/ ──→ data/ ──→ kernel/ServiceRegistry
     │                  │                  │
     │                  ├──→ kernel/ (ServerClient)
     │                  └──→ hal/ (FingerprintDriver)
     │
     ├──→ hal/ (SSD1306Driver)
     └──→ kernel/ (WiFiManager)

kernel/ ──→ data/ (StorageManager for NVS creds)
hal/     ──→ config/ (pin definitions)
```

All layers depend on `kernel/ServiceRegistry.h` for type definitions. Domain services never depend on presentation. HAL drivers are standalone (only depend on `config/Config.h`).
