---
type: Kernel
title: Power Management
description: Light/deep sleep state machine, wake sources, activity tracking, RTC memory persistence, and configurable thresholds.
tags: [power, sleep, rtc-memory, wake-sources]
timestamp: 2026-06-29T00:00:00Z
---

# Power Management

## Power States

```
PM_ACTIVE
  ├── idle > lightSleepThreshold (default 10s)
  │     → PM_LIGHT_SLEEP
  │     → esp_light_sleep_start()
  │     → wake: MPU INT (GPIO35) or timer
  │     → restore state → PM_ACTIVE
  │
  └── idle > deepSleepThreshold (default 60s)
        → PM_DEEP_SLEEP
        → save baseline to RTC memory
        → WiFi.disconnect(), mDNS.end()
        → esp_deep_sleep_start()
        → wake: timer only → full cold boot
```

## Activity Tracking

Idle timer reset on every activity event:

| Source | Message | Trigger |
|--------|---------|---------|
| WeightService | ACTIVITY | Each successful HX711 reading |
| MotionService | MOTION_WAKE | Any motion detected |
| WebServer | (not yet wired) | HTTP request |
| AccessController | (not yet wired) | Fingerprint scan |

Drained in `loop()`:
```cpp
ServiceMessage pmMsg;
while (g_registry.tryReceive(ServiceId::POWER, pmMsg)) {
    switch ((KernelMsgType)pmMsg.type) {
        case ACTIVITY: powerManager.onActivity(); break;
        case MOTION_WAKE: powerManager.handleWakeFromMotion(); break;
    }
}
powerManager.update();
```

## Configurable Thresholds

| Parameter | Default | NVS Key | API Endpoint |
|-----------|---------|---------|--------------|
| Light sleep timeout | 10000ms | `cfg_light` | POST /api/config |
| Deep sleep timeout | 60000ms | `cfg_deep` | POST /api/config |

## Mode-Specific Behavior

| Feature | AP Mode | STA Mode |
|---------|---------|----------|
| CPU frequency | 240 MHz fixed | DFS 80-240 MHz |
| Light sleep | Disabled | Enabled (auto-DVFS) |
| Deep sleep | Disabled | Enabled |
| `sleepAllowed` | false | true |

## Wake Sources

### Light Sleep
- **GPIO35 (MPU INT)** — motion wakes system
- **Timer** — periodic wake for state checks

### Deep Sleep
- **Timer only** — MPU INT does NOT survive deep sleep
- Full cold boot: `setup()` runs again from scratch

## RTC Memory (survives deep sleep)

Stored in RTC slow memory (preserved across deep sleep):
- `g_bootCount` — incremented each boot
- `wakeCount` — incremented each wake
- `savedBaseline` — weight baseline survives power cycle
- `needRecalibration` — flag set if recalibration needed post-wake

## PowerManager API

```cpp
class PowerManager {
    void begin(bool setupWake = true);
    void update();               // checks idle time, triggers sleep
    void onActivity();           // resets idle timer
    void enterLightSleep();
    void exitLightSleep();
    void enterDeepSleep();
    void handleWakeFromMotion();
    void handleWakeFromTimer();
    void handleColdBoot();
    PowerState getCurrentState();
    int getWakeCount();
    float getBaseline();
    void setBaseline(float baseline);
    void setThresholds(unsigned long lightMs, unsigned long deepMs);
    void setOperationalMode(OperationalMode mode);
    bool isSleepAllowed();
};
```

# Citations

[1] src/kernel/PowerManager.h — Class declaration
[2] src/kernel/PowerManager.cpp — Implementation
[3] src/main.cpp — loop() power drain
[4] docs/tasks-and-timing.md — Timing documentation
