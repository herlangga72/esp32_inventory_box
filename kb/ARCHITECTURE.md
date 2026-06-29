---
type: Architecture
title: System Architecture
description: Microkernel service registry pattern — single g_registry struct at fixed link-time address, no heap after boot, no virtual functions.
tags: [architecture, kernel, freertos, patterns]
timestamp: 2026-06-29T00:00:00Z
---

# System Architecture

## The Core Principle

A single `g_registry` struct (~73 KB in .bss) at a fixed link-time address holds ALL service state in compile-time-sized pools. No heap allocation after boot. No virtual functions. No STL.

```cpp
extern ServiceRegistry g_registry;  // single instance, file scope
```

## Registry Layout

```
g_registry (ServiceRegistry)
├── Header (16B)            — magic=0x53455256, version, uptime, bootCount
├── SCB[22] (792B)          — ServiceControlBlock per service
├── kernelPool (16384)       — Logger, Storage, WiFi, Power, SystemStatus
├── halPool (2048)           — HX711, MPU6050, SSD1306, LCD1602, Fingerprint
├── domainPool (1024)        — WeightService, MotionService, StateManager,
│                              AccessController, DoorService
├── dataPool (8192)          — ToolRepo, UserRepo, LogRepo
├── presentationPool (1024)  — WebServer, DisplayManager, SerialCLI, ServerClient
├── queueBufferPool (4096)   — Static FreeRTOS queue buffers
├── queueStructs[22]         — StaticQueue_t structs
├── wifiPool (4096)          — lwIP internal buffers
├── taskStackPool (16384)    — Static stacks for 6 domain tasks
├── taskTCBs[6]              — StaticTask_t structs
├── netArena (1536)          — WebServer sockets, HTTP buffers
├── loggerArena (1536)       — Logger queue + task stack
├── fsArena (1024)           — SPIFFS temp workspace
├── ioArena (1024)           — I2C/UART driver scratch
├── stringArena (512)        — Temp String ring buffer
├── fpArena (512)            — Adafruit_Fingerprint extra buffer
└── sysArena (512)           — FreeRTOS mutex/semaphore prealloc
```

## Layer Dependencies

```
presentation/ ──→ domain/services/ ──→ data/ ──→ kernel/ServiceRegistry
     │                  │                  │
     │                  ├──→ kernel/ServerClient
     │                  └──→ hal/FingerprintDriver
     ├──→ hal/SSD1306Driver
     └──→ kernel/WiFiManager

kernel/ ──→ data/ (StorageManager for NVS creds)
hal/     ──→ config/ (pin definitions)
```

## Task Architecture

7 FreeRTOS tasks, all on Core 0 except Display on Core 1:

| Task | Pri | Core | Stack | AP Rate | STA Rate | Condition |
|------|-----|------|-------|---------|----------|-----------|
| State | 10 | 0 | 1536 | 20 Hz | 5 Hz | Always |
| Access | 9 | 0 | 2560 | 5 Hz | 2 Hz | Fingerprint OK |
| Weight | 8 | 0 | 1536 | 10 Hz | 2 Hz | HX711 OK |
| Motion | 8 | 0 | 1536 | 100 Hz | 10 Hz | MPU6050 OK |
| WiFi | 6 | 0 | 6144* | 100 Hz | 10 Hz | Always |
| Web | 5 | 0 | 2560 | 1 kHz | 20 Hz | WebServer OK |
| Display | 3 | 1 | 1536 | 1 Hz | 0.2 Hz | Display OK |

*WiFi uses heap (lwIP requirement). Others use static stacks from `taskStackPool`.

Dynamic timing via `opDelay(apMs, staMs)` — reads `OperationalMode` from SystemStatus.

## Free Functions on Pool Memory

Domain services are NOT classes. They are free functions operating on `*Memory` structs:

```cpp
void ws_onRawReading(WeightServiceMemory* mem, int32_t raw);
void ws_update(WeightServiceMemory* mem);
float ws_getCurrentWeight(const WeightServiceMemory* mem);

// Caller gets typed pointer from registry
auto* wm = g_registry.getWeightService();
ws_onRawReading(wm, rawValue);
```

## Message-Driven IPC

16-byte `ServiceMessage` via FreeRTOS queues. No direct function calls between domain services.

```
WeightService ──WEIGHT_CHANGE──► StateManager
MotionService ──MOTION_DETECTED─► StateManager
StateManager ──STATE_CHANGED──► DisplayManager
AccessController ──USER_LOGIN──► StateManager
AccessController ──UNLOCK──► DoorService
Any service ──ACTIVITY──► PowerManager
```

## Graceful Degradation

Every hardware component init via `initWithRetry()` with adaptive backoff (1x, 2x, 4x base, cap 2s):

1. Component marked ERROR in SystemStatus
2. Interrupts disabled for that sensor
3. Dependent task not created (vTaskDelete)
4. System continues with reduced functionality

Runtime recovery: Weight/Motion tasks retry failed sensors every 5s for 10 attempts.

## Dual Publish Pattern

State changes publish to BOTH mailbox (new) AND EventBus (legacy):

```cpp
EventBus::getInstance()->publish(event);  // legacy
g_registry.send(ServiceId::DISPLAY_MANAGER, dm);  // new
```

## Key Design Constraints

1. **No heap after boot** — all state in compile-time pools. Unavoidable: Preferences (NVS), WebServer (Arduino), SPIFFS File objects.
2. **No virtual functions** — free functions + structs. Only virtual dispatch inside Arduino/Adafruit libs.
3. **Static queues** — `xQueueCreateStatic` with pool buffers.
4. **ISR-safe** — `g_registry.sendFromISR()` with `pxHigherPriorityTaskWoken`.
5. **Safe-fail lock** — relay active LOW. Power loss = door stays locked.
6. **Conditional tasks** — failed sensor = no task = no wasted CPU.

# Citations

[1] [Service Registry](/kb/kernel/service-registry.md) — IPC details
[2] [Boot Sequence](/kb/kernel/boot-sequence.md) — Init stages
[3] docs/architecture.md — Written architecture doc
