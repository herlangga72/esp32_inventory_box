# Tasks, Timing, and Boot Sequence

## FreeRTOS Configuration

- **Unicore**: All tasks on Core 0 except Display (Core 1)
- **Tick rate**: 1 kHz (1ms resolution)
- **Tickless idle**: enabled (saves power during idle periods)
- **Scheduler**: preemptive, priority-based

## Task Table

| Task | Priority | Core | Stack | AP Rate | STA Rate | Conditional On |
|------|----------|------|-------|---------|----------|----------------|
| State | 10 | 0 | 2560 | 20 Hz (50ms) | 5 Hz (200ms) | Never skipped |
| Access | 9 | 0 | 4096 | 5 Hz (200ms) | 2 Hz (500ms) | Fingerprint OK |
| Weight | 8 | 0 | 2560 | 10 Hz (100ms) | 2 Hz (500ms) | HX711 OK |
| Motion | 8 | 0 | 2560 | 100 Hz (10ms) | 10 Hz (100ms) | MPU6050 OK |
| WiFi | 6 | 0 | 6144 | 100 Hz (10ms) | 10 Hz (100ms) | Never skipped |
| Web | 5 | 0 | 4096 | 1 kHz (1ms) | 20 Hz (50ms) | WebServer OK |
| Display | 3 | 1 | 2560 | 1 Hz (1s) | 0.2 Hz (5s) | Display OK |

**AP mode**: full-speed rates. **STA mode**: power-saving slower rates.

### Dynamic Timing

```cpp
inline TickType_t opDelay(int apMs, int staMs) {
    return (ss_getOperationalMode(g_registry.getSystemStatus()) == OperationalMode::OP_AP_FULL)
        ? pdMS_TO_TICKS(apMs)
        : pdMS_TO_TICKS(staMs);
}
```

Single code path, two timing profiles. Mode switched by WiFi manager at boot and on STA disconnect → AP fallback.

## Task Details

### State Task (Priority 10, Core 0)

Highest priority domain task. Primary hub for all domain events.

```
loop:
  1. Drain all pending mailbox messages (non-blocking)
  2. sm_updatePeriodic() — time-based transitions
  3. Block on mailbox receive with timeout (50ms AP / 200ms STA)
  4. On wake: dispatch message + drain remaining
  5. sm_updatePeriodic() again
  6. heartbeat
```

Dual `sm_updatePeriodic()` calls ensure time-based transitions fire even when no messages arrive.

### Access Task (Priority 9, Core 0)

```
loop:
  1. Drain AccessController mailbox (enrollment, delete, remote unlock cmds)
  2. Drain DoorService mailbox
  3. ac_update() — runs access state machine
  4. ds_update() — runs door state machine (relay timer, reed monitor)
  5. serverClient.update() — heartbeat + retry state machine
  6. delay 200ms (AP) / 500ms (STA)
```

AccessController and DoorService share one task because DoorService is mostly passive (relay timer + reed polling) and AccessController drives the unlock flow.

### Weight Task (Priority 8, Core 0)

```
loop:
  1. Wait for HX711 DRDY interrupt signal
  2. hx711.readRaw() → ws_onRawReading(raw)
  3. ws_update() — drain mailbox (baseline, calibration, tare commands)
  4. send ACTIVITY to PowerManager
  5. heartbeat
  6. delay 100ms (AP) / 500ms (STA)

Sensor retry: if HX711 failed at boot, task retries every 5s for 10 attempts.
On recovery → resumes normally. On final failure → vTaskDelete(NULL).
```

### Motion Task (Priority 8, Core 0)

```
loop:
  1. Wait for MPU6050 INT interrupt
  2. ms_update(&mpu) — read accel, classify, send events
  3. send MOTION_WAKE to PowerManager
  4. heartbeat
  5. delay 10ms (AP) / 100ms (STA)

Sensor retry: same 10-retry pattern as Weight task.
```

### WiFi Task (Priority 6, Core 0)

```
loop:
  1. wifiManager.update()
     - AP mode: configPortal->handleClient()
     - STA mode: check connection every 5s, reconnect state machine
  2. In STA mode (debug): ArduinoOTA.handle() + cli.handle()
  3. delay 10ms (AP) / 100ms (STA)
```

Largest stack (6144) — WiFi stack + WebServer config portal + OTA + CLI all in one task.

### Web Task (Priority 5, Core 0)

```
loop:
  1. web_handle() — server.handleClient() (single client per tick)
  2. delay 1ms (AP) / 50ms (STA)
```

WebServer uses Arduino's blocking `handleClient()`. Fast poll rate in AP mode for responsive config portal. Skipped entirely in AP mode (config portal handles instead).

### Display Task (Priority 3, Core 1)

Only task on Core 1. Low priority — never blocks critical path.

```
loop:
  1. Drain display mailbox messages
  2. dm_update(&oled):
     - Health check every 5s (I2C ping, reinit on fail)
     - 1 Hz refresh rate limit
     - Sleep/wake OLED based on awake flag
     - Draw current screen + notification overlay
  3. delay 1s (AP) / 5s (STA)
```

## WiFi State Machine

### Boot
```
hasStoredCredentials? ──YES──→ connectSTA()
    │                            ├─ success → STA mode, OP_STA_IDLE
    │                            └─ fail → startConfigPortal()
    NO                                      AP mode, OP_AP_FULL
    │
    └─→ startConfigPortal()
         AP mode, OP_AP_FULL
```

### STA Connect
- 15s timeout
- Blocks during boot only. Non-blocking reconnect after boot.
- `WiFi.setSleep(false)` — modem sleep disabled (prevents random disconnects)

### STA Reconnect (non-blocking)
```
every 5s: WiFi.status() check
    connected → nothing
    disconnected → reconnecting = true, WiFi.reconnect()
        ├─ reconnects within 10s → log success, continue STA
        └─ 10s timeout → fall back to AP mode
            WiFi.mode(WIFI_OFF) → startConfigPortal()
            OperationalMode → OP_AP_FULL
            All task rates switch to AP speed
```

### AP Config Portal
- SSID: `Inventory-Box-Setup`, no password
- Channel: 6 (fallback to 1 if channel 6 fails)
- 20 MHz b/g/n, 19.5 dBm TX power
- IP: 192.168.4.1
- Captive portal: all routes → `wifi-setup.html` from SPIFFS (or inline HTML fallback)
- POST `/save` → stores creds in NVS → reboot
- GET `/scan` → async WiFi scan (non-blocking poll model)

## Power Management

### Modes
| Mode | AP | STA |
|------|-----|-----|
| CPU freq | Locked 240 MHz | DFS 80-240 MHz |
| Light sleep | Disabled | Enabled (auto-DVFS) |
| Deep sleep | Disabled | Enabled |
| `sleepAllowed` | false | true |

### State Machine
```
PM_ACTIVE
    │ onActivity() resets lastActivityTime
    │
    ├─ idle > lightSleepThreshold (default 10s)
    │    → PM_LIGHT_SLEEP → esp_light_sleep_start()
    │    wake sources: GPIO35 (MPU INT), timer
    │    on wake: exitLightSleep() → PM_ACTIVE
    │
    └─ idle > deepSleepThreshold (default 60s)
         → PM_DEEP_SLEEP
         save baseline to RTC memory
         WiFi.disconnect(), mDNS.end()
         esp_deep_sleep_start()
         wake: timer only → full cold boot (setup() runs again)
```

### Activity Sources
- WeightService: sends ACTIVITY on each successful reading
- MotionService: sends MOTION_WAKE on motion detection
- WebServer: (not yet wired — would need mailbox send on request)
- AccessController: (not yet wired)

### `loop()` PowerManager Integration
```cpp
void loop() {
    // Drain PowerManager mailbox
    ServiceMessage pmMsg;
    while (g_registry.tryReceive(ServiceId::POWER, pmMsg)) {
        switch (pmMsg.type) {
            case ACTIVITY: powerManager.onActivity(); break;
            case MOTION_WAKE: powerManager.handleWakeFromMotion(); break;
        }
    }
    powerManager.update();  // checks idle time, triggers sleep
}
```

### RTC Memory (survives deep sleep)
- `g_bootCount` — incremented on each boot
- `wakeCount` — incremented on each wake
- `savedBaseline` — weight baseline preserved across deep sleep
- `needRecalibration` — flag set if calibration needed after wake

## Boot Sequence

### setup() — Step by Step

```
1. LED BLINK (3 fast pulses, then 2s solid)
2. BOOT BUTTON CHECK
   - If held (LOW): wipe wifi_ssid + wifi_pass from NVS (force AP mode)
3. SERIAL + LOGGER INIT (115200 baud, log queue priority 2, stack 2560)
4. g_registry.init()
   - memset entire struct to 0
   - set magic=0x53455256, version=1, bootCount
   - init all 22 SCBs to UNINIT state
5. ss_begin() — init SystemStatus, pre-register 10 component names
6. HARDWARE INIT (each via initWithRetry):
   ├─ Storage (NVS)        — 0 retries, must succeed
   ├─ HX711                — 3 retries, 2000ms delay
   ├─ MPU6050              — 3 retries, 1500ms delay
   ├─ Display (SSD1306)    — 2 retries, 2000ms delay
   └─ Fingerprint (R307)   — 3 retries, 2000ms delay
7. INTERRUPT CONFIG
   - InterruptManager::begin()
   - Disable interrupts for failed sensors (HX711 DRDY, MPU INT)
8. MAILBOX REGISTRATION (all static queues)
   STATE_MANAGER(32), WEIGHT_SERVICE(8), ACCESS_CONTROLLER(16),
   DOOR_SERVICE(8), DISPLAY_MANAGER(16), POWER(8), MOTION_SERVICE(4)
9. SERVICE INIT
   - WeightService: calibration factor, filter size, load baseline from NVS
   - MotionService: capture resting accel from MPU (if available)
   - StateManager: sm_init()
   - Repositories: tr_init(), ur_init()
   - PowerManager: begin(hasMPU)
10. WIFI
    - wifiManager.begin()
    - STA connect or AP fallback
    - Set OperationalMode on both SystemStatus and PowerManager
11. WEB SERVER (STA only)
    - web_begin() — register all routes, start HTTP server
12. FINGERPRINT (already initted in step 6, just status check)
13. DOOR SERVICE — ds_begin() (relay + reed switch init)
14. SERVER CLIENT — begin() if URL configured
15. ACCESS CONTROLLER — ac_init(), load local fallback config
16. DISPLAY MANAGER — set healthy/awake flags
17. mDNS + OTA (debug builds only)
18. COMPONENT STATUS SUMMARY (serial log)
19. TASK CREATION (conditional — skip tasks for failed components)
20. ss_setBootComplete()
```

### initWithRetry() Pattern
```cpp
bool initWithRetry(const char* name, std::function<bool()> initFn,
                   const char* errorMsg, int maxRetries = 3, int retryDelayMs = 1000) {
    for (int attempt = 0; attempt <= maxRetries; attempt++) {
        if (initFn()) {
            ss_markOK(ss, name);
            return true;
        }
        if (attempt < maxRetries) {
            delay(retryDelayMs); yield();
        }
    }
    ss_markError(ss, name, errorMsg);
    return false;
}
```

### Sensor Runtime Recovery
Weight and Motion tasks have post-boot retry logic:
- If component marked ERROR at boot → task loops retrying every 5s (up to 10 attempts)
- On recovery → `ss_markOK()` → continues normally
- On final failure → `vTaskDelete(NULL)` — task exits cleanly
- Uses `goto` to jump into main loop after recovery (`weight_start:`, `motion_start:`)

### LED Status Patterns

| Pattern | Meaning |
|---------|---------|
| Solid ON (LOW) | AP mode active |
| 3s blink | STA connected, all OK |
| 200ms fast blink | STA connected, but errors/warnings present |
| 500ms blink | STA mode, not connected |

LED is active LOW (built-in blue LED on most ESP32 dev boards).
