---
type: Kernel
title: Service Registry — Microkernel IPC
description: ServiceRegistry struct, ServiceMessage 16-byte IPC, ServiceControlBlock, pool allocation, message type enums, arena allocators.
tags: [kernel, ipc, registry, messaging, queues]
timestamp: 2026-06-29T00:00:00Z
---

# Service Registry — Microkernel IPC

## The Global

```cpp
extern ServiceRegistry g_registry;  // single instance, file scope, fixed link-time address
```

Used by every subsystem. Address logged at boot: `"Registry at 0x3FFB...."`.

## ServiceId Enum

| ID | Service | Mailbox Depth |
|----|---------|---------------|
| 1 | LOGGER | — |
| 2 | STORAGE | — |
| 3 | WIFI | — |
| 4 | POWER | 8 |
| 5 | SYSTEM_STATUS | — |
| 6 | STATE_MANAGER | 32 |
| 7 | WEIGHT_SERVICE | 8 |
| 8 | MOTION_SERVICE | 4 |
| 9 | ACCESS_CONTROLLER | 16 |
| 10 | DOOR_SERVICE | 8 |
| 11 | MATCHING_SERVICE | (stateless) |
| 12-14 | TOOL/USER/LOG_REPOSITORY | — |
| 15 | WEB_SERVER | — |
| 16 | DISPLAY_MANAGER | 16 |
| 17-18 | SERIAL_CLI / SERVER_CLIENT | — |
| 19-22 | HX711 / MPU6050 / SSD1306 / FINGERPRINT | — |

## ServiceControlBlock (36 bytes)

```cpp
struct ServiceControlBlock {
    uint8_t       id;                    // ServiceId
    uint8_t       state;                 // UNINIT/INIT/RUNNING/ERROR/STOPPED
    uint16_t      flags;                 // HAS_INBOX | HAS_TASK | IS_PASSIVE | HEARTBEAT
    QueueHandle_t inbox;                 // FreeRTOS queue (static allocation)
    uint32_t      msg_received;          // monotonic
    uint32_t      msg_sent;
    uint32_t      msg_dropped;
    uint32_t      last_heartbeat_ms;
    TaskHandle_t  task;
    uint32_t      owned_memory_offset;
    uint16_t      owned_memory_size;
    uint16_t      reserved;
};
```

## ServiceMessage (16 bytes)

```cpp
struct ServiceMessage {
    uint8_t target;    // ServiceId
    uint8_t type;      // message type enum value
    uint8_t replyTo;   // for request-reply
    uint8_t corrId;    // correlation ID
    union {
        struct { float f1; float f2; }                f2;    // 2 floats
        struct { float f1; uint16_t u1, u2; }          f1u2;  // float + 2 u16
        struct { uint32_t u32_1, u32_2; }              u32;   // 2 u32
        struct { uint16_t u1, u2; }                    u2;
        struct { uint16_t u1, u2, u3, u4; }            u4;    // 4 u16
        struct { uint8_t b0..bb; }                     bytes; // 12 bytes
        uint8_t raw[12];
    };

    static ServiceMessage cmd(target, type);     // fire-and-forget
    static ServiceMessage query(target, replyTo, type, corrId); // request-reply
};
```

## Messaging API

```cpp
// Fire-and-forget
g_registry.sendCmd(ServiceId::POWER, (uint8_t)KernelMsgType::ACTIVITY);

// With float payload
ServiceMessage msg = ServiceMessage::cmd(ServiceId::STATE_MANAGER,
    (uint8_t)StateMsgType::WEIGHT_CHANGE);
msg.f2.f1 = delta;
msg.f2.f2 = currentWeight;
g_registry.send(ServiceId::STATE_MANAGER, msg);

// From ISR
BaseType_t woken = pdFALSE;
g_registry.sendFromISR(ServiceId::STATE_MANAGER, msg, &woken);
if (woken) portYIELD_FROM_ISR();

// Request-reply
ServiceMessage reply;
g_registry.query(target, req, reply, pdMS_TO_TICKS(500));

// Non-blocking drain
while (g_registry.tryReceive(ServiceId::STATE_MANAGER, msg)) {
    sm_dispatchMessage(mem, msg);
}
```

## Message Type Enums

### KernelMsgType
| Value | Name | Payload | Sender |
|-------|------|---------|--------|
| 1 | ACTIVITY | — | Any task |
| 2 | MOTION_WAKE | — | MotionService |
| 3 | ENTER_SLEEP | — | PowerManager |
| 4 | SET_BASELINE | f2.f1 | StateManager → WeightService |
| 5 | SET_OPERATIONAL_MODE | bytes.b0 | WiFiManager |
| 10-12 | STORAGE_GET/PUT/REMOVE | — | Repos |
| 20-21 | WIFI_STATUS_QUERY / STATUS_QUERY | — | Any |

### StateMsgType
| Value | Name | Payload | Sender |
|-------|------|---------|--------|
| 1 | WEIGHT_CHANGE | f2.f1=delta, f2.f2=weight | WeightService |
| 2 | MOTION_DETECTED | bytes.b0=MotionType | MotionService |
| 3 | TOOL_MATCHED | u2.u1=toolId | Internal |
| 4 | UNKNOWN_WEIGHT | f2.f1=weight | Internal |
| 5 | USER_LOGIN | u4.u1=userId | AccessController, WebServer |
| 6 | USER_LOGOUT | — | WebServer |
| 7 | CALIBRATION | f2.f1=baseline | WebServer |
| 8 | ENTER_SLEEP | — | PowerManager |
| 9 | WAKE | — | PowerManager |

### WeightMsgType
| Value | Name | Payload |
|-------|------|---------|
| 1 | SET_BASELINE | f2.f1 |
| 2 | START_CALIBRATION | u4.u1=sampleCount |
| 3 | TARE | — |

### AccessMsgType
| Value | Name | Payload |
|-------|------|---------|
| 1 | BEGIN_ENROLLMENT | u4.u1=fpId |
| 2 | CANCEL_ENROLLMENT | — |
| 3 | DELETE_FINGERPRINT | u4.u1=fpId |
| 4 | DELETE_ALL_FP | — |
| 5 | REMOTE_UNLOCK | — |

### DoorMsgType
| Value | Name | Payload |
|-------|------|---------|
| 1 | UNLOCK | u32.u32_1=duration(opt) |
| 2 | LOCK | — |
| 3 | SET_DURATION | u32.u32_1=durationMs |

## Pool Allocation

Every service memory struct has a compile-time offset:

```cpp
#define SR_OFFSET_WEIGHT_SERVICE (SR_OFFSET_DOMAIN_POOL + 0)
#define SR_OFFSET_MOTION_SERVICE (SR_OFFSET_DOMAIN_POOL + sizeof(WeightServiceMemory))
// ...

WeightServiceMemory* ServiceRegistry::getWeightService() {
    return reinterpret_cast<WeightServiceMemory*>(&domainPool[0]);
}
```

Capacity verified at compile-time:
```cpp
static_assert(SR_POOL_DOMAIN_SIZE >=
    sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory) + ...,
    "Domain pool too small");
```

## Arena Allocators (Bump, Never Free)

For subsystems needing dynamic allocation:

```cpp
void* netAlloc(size_t n);     // 1536B — WebServer sockets
void* loggerAlloc(size_t n);  // 1536B — Logger queue
void* fsAlloc(size_t n);      // 1024B — SPIFFS
void* ioAlloc(size_t n);      // 1024B — I2C/UART
void* stringAlloc(size_t n);  // 512B  — String buffer
void* fpAlloc(size_t n);      // 512B  — Fingerprint
void* sysAlloc(size_t n);     // 512B  — Semaphores
```

Aligned bump allocation. Never freed. Returns nullptr if exhausted.

## MailboxSystem — Legacy Shim

```cpp
MailboxSystem::getInstance().send(id, msg);  // ≡ g_registry.send(id, msg)
```

Used by WebServer.cpp (legacy code). New code uses g_registry directly.

# Citations

[1] src/kernel/ServiceRegistry.h — Full header with all structs and enums
[2] src/kernel/ServiceRegistry.cpp — Implementation
[3] src/kernel/MailboxSystem.h — Legacy shim
