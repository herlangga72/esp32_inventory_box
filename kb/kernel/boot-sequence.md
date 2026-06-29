---
type: Kernel
title: Boot Sequence
description: Complete boot sequence with dual-core parallel init, initWithRetry pattern, conditional task creation, and component status summary.
tags: [boot, initialization, dual-core, tasks]
timestamp: 2026-06-29T00:00:00Z
---

# Boot Sequence

## setup() — Step by Step

### Phase 0: Pre-init
```
1. LED: 3 fast blinks → 2s solid → off
2. BOOT button check:
     - Held (LOW): 5 rapid blinks, wipe wifi_ssid+wifi_pass from NVS
     - Released: continue
3. Serial(115200) + logInit(priority=2, stack=2560)
```

### Phase 1: Registry + System Status
```
4. g_registry.init()
     - memset entire struct to 0
     - magic = 0x53455256, version = 1
     - increment bootCount
     - init all 22 SCBs to UNINIT state
5. ss_begin() → register 11 component slots:
     Storage, HX711, MPU6050, Display, WiFi, WebServer,
     Fingerprint, Door, ServerClient, AccessController, RTC
6. I2C pullups: pinMode(SDA, INPUT_PULLUP), pinMode(SCL, INPUT_PULLUP)
```

### Phase 2: Dual-Core Parallel Init

Core 1 (this core, runs setup()):
```
Storage (NVS) — 0 retries, must succeed
RTC (DS3231) — 3 retries, 1s delay; fallback: compile-time + uptime
HX711 — 3 retries, 300ms backoff (1x, 2x, 4x)
  Wait for Core 0 I2C_DONE signal
```

Core 0 (bootCore0Worker task, prio 12):
```
MPU6050 — 3 retries, 200ms backoff
Display auto-detect:
  - Probe LCD (0x27, 0x3F)
  - If LCD: Lcd1602Driver::init(addr), DisplayType::LCD1602
  - Else probe SSD1306 (0x3C):
    - If found: SSD1306Driver::init(), DisplayType::SSD1306
    - Else: mark ERROR, DisplayType::SSD1306 (fallback)
  Signal I2C_DONE
  Wait for FP_GO signal from Core 1
Fingerprint (R307) — 3 retries, 300ms backoff
  Signal FP_DONE
  vTaskDelete(NULL)
```

### Phase 3: Interrupts + Mailboxes
```
7. InterruptManager::begin()
8. Disable interrupts for failed sensors:
     HX711 DRDY ERROR → gpio_isr_handler_remove, set GPIO_MODE_DISABLE
     MPU6050 INT ERROR → same
9. Register mailboxes (static queues):
     STATE_MANAGER(32), WEIGHT_SERVICE(8), ACCESS_CONTROLLER(16),
     DOOR_SERVICE(8), DISPLAY_MANAGER(16), POWER(8), MOTION_SERVICE(4)
```

### Phase 4: Service Init
```
10. WeightService:
      calibrationFactor = Config::CALIBRATION_FACTOR
      filterSize = Config::FILTER_SIZE
      hx711.begin()
11. MotionService (if MPU6050 OK):
      read resting accel: restingAccel[2] -= 1.0f (gravity compensation)
12. Load baseline from NVS → WeightService + StateManager
13. sm_init() — StateManager
14. tr_init() — ToolRepository
15. PowerManager::begin(hasMPU)
```

### Phase 5: Network + Peripherals
```
16. WiFi:
      wifiManager.begin()
      → STA connect or AP fallback
      → Set OperationalMode on SystemStatus + PowerManager
17. WebServer (STA only):
      web_begin() → register all routes, server.begin()
      In AP mode: configPortal handles, no WebServer
18. Signal Core 0 to start Fingerprint (FP_GO)
19. DoorService: ds_begin() → relay init, reed switch init
20. ServerClient: begin() if URL configured
21. AccessController: ac_init(), load local fallback config
22. DisplayManager: set healthy/awake flags
23. mDNS + OTA (debug builds only)
24. Wait for Core 0 FP_DONE signal
```

### Phase 6: Task Creation
```
25. Create tasks (conditional):
      WiFi — heap stack (6144), always created
      State — static stack (1536), always
      Access — static (2560), if Fingerprint OK
      Weight — static (1536), if HX711 OK
      Motion — static (1536), if MPU6050 OK
      Web — static (2560), if WebServer OK
      Display — static (1536), Core 1, if Display OK
26. ss_setBootComplete()
```

## initWithRetry Pattern

```cpp
template<typename F>
bool initWithRetry(const char* name, F initFn,
                   const char* errorMsg, int maxRetries, int baseDelayMs) {
    for (int attempt = 0; attempt <= maxRetries; attempt++) {
        if (initFn()) {
            if (attempt > 0) LOG_INFO("INIT", "OK (recovered)");
            ss_markOK(ss, name);
            return true;
        }
        if (attempt < maxRetries) {
            // Adaptive backoff: 1x, 2x, 4x base (cap 2s)
            delay(baseDelayMs * (1 << attempt));
            yield();
        }
    }
    ss_markError(ss, name, "DISABLED (exceeded retries)");
    return false;
}
```

On failure: component marked ERROR, interrupt disabled, task not created.
System continues with reduced functionality.

## Sensor Runtime Recovery

Weight and Motion tasks retry failed sensors at runtime:
```cpp
if (ss_getStatus(ss, "HX711") == ERROR) {
    for (int retry = 1; retry <= 10; retry++) {
        vTaskDelay(5000);
        hx711.begin();
        if (hx711.readRaw() != INT32_MIN) {
            ss_markOK(ss, "HX711");
            goto weight_start;  // jump into main loop
        }
    }
    vTaskDelete(NULL);  // give up
}
```

## WiFi Boot State Machine

```
hasStoredCreds?
  YES → connectSTA(15s timeout)
    ├── success → OP_STA_IDLE, slow task rates
    └── fail → startConfigPortal(), OP_AP_FULL
  NO → startConfigPortal(), OP_AP_FULL

STA reconnect (non-blocking):
  every 5s check WiFi.status()
    connected → nothing
    disconnected → WiFi.reconnect()
      ├── within 10s → log, continue STA
      └── timeout → AP fallback
```

## Component Status Summary

At end of boot (always logged):
```
===========================================
COMPONENT STATUS:
-------------------------------------------
Storage:     [OK]
HX711:       [OK]
MPU6050:     [FAIL]
...
```

If any ERROR: `"System will continue with reduced functionality"`

# Citations

[1] src/main.cpp — Full implementation
[2] src/kernel/WiFiManager.cpp — WiFi state machine
[3] docs/tasks-and-timing.md — Task rate documentation
