# RTC Module with Uptime Fallback

**Date:** 2026-06-23
**Status:** approved

## Goal

Add DS3231 RTC module over I2C_NUM_1. On init failure, fall back to compile-time
+ uptime clock. All existing `time(nullptr)` calls keep working.

## Architecture

```
Boot → RtcDriver::init() over I2C_NUM_1 (pins 18/23)
  ├─ OK: read DS3231 → settimeofday() → system clock set
  └─ FAIL: settimeofday(__DATE__ + __TIME__) → uptime-based clock
```

## Files

| File | Action | Purpose |
|------|--------|---------|
| `src/hal/RtcDriver.h` | NEW | DS3231 init + time sync |
| `src/hal/RtcDriver.cpp` | NEW | I2C read, settimeofday, readable getter |
| `src/config/Config.h` | EDIT | Pins, I2C address, bus number |
| `src/main.cpp` | EDIT | RTC init in boot sequence |
| `src/kernel/ServiceRegistry.h` | EDIT | Add RTC to SystemStatus components |

## RtcDriver API

```cpp
bool rtc_init();                  // Init I2C_NUM_1, probe DS3231, sync system clock
time_t rtc_now();                 // Read current time from DS3231 (raw)
bool rtc_isAvailable();           // Did init succeed?
```

## Pin assignment

- I2C_NUM_1 SDA: GPIO 18
- I2C_NUM_1 SCL: GPIO 23
- DS3231 address: 0x68

## Boot sequence insertion

After Storage init, before HX711. RTC time needed early for logging
timestamps. Failure is non-fatal — system runs with uptime clock.

## Graceful degradation

- `initWithRetry()` pattern — 3 retries, 1s delay
- On failure: `SS_ERROR` on RTC component, system continues
- `time(nullptr)` returns compile-time + uptime offset
