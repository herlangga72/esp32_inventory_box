---
type: Hardware Map
title: Pin Assignments and Bus Topology
description: Complete ESP32 pin mapping, I2C bus topology, and hardware driver summary.
tags: [hardware, pins, i2c, uart, gpio]
timestamp: 2026-06-29T00:00:00Z
---

# Hardware Map

## Pin Table

| Pin | Function | Type | Notes |
|-----|----------|------|-------|
| **Weight (HX711)** ||||
| 16 | HX711 DT | Input | Load cell data |
| 17 | HX711 SCK | Output | Load cell clock |
| 36 | HX711 DRDY | Input | Data ready interrupt (ADC-only, no pull) |
| **Motion (MPU6050)** ||||
| 21 | MPU6050 SDA | I2C | Shared with SSD1306 (I2C Bus 0) |
| 22 | MPU6050 SCL | I2C | Shared with SSD1306 (I2C Bus 0) |
| 35 | MPU INT | Input | Motion interrupt |
| **Display** ||||
| 21 | SSD1306 SDA | I2C | Shared with MPU6050 (I2C Bus 0) |
| 22 | SSD1306 SCL | I2C | Shared with MPU6050 (I2C Bus 0) |
| 19 | OLED RST | Output | Display reset |
| **Fingerprint (R307)** ||||
| 5 | FP RX | UART RX | Remapped from default Serial2 |
| 4 | FP TX | UART TX | Remapped from default Serial2 |
| **Door** ||||
| 13 | Relay | Output | Solenoid lock, active LOW |
| 14 | Door Sensor | Input | Reed switch, INPUT_PULLUP, LOW=closed |
| **RTC (DS3231)** ||||
| 18 | RTC SDA | I2C | I2C_NUM_1, dedicated bus |
| 23 | RTC SCL | I2C | I2C_NUM_1, dedicated bus |
| **System** ||||
| 33 | Button | Input | BOOT, INPUT_PULLUP, LOW=pressed |
| 2 | LED | Output | Built-in blue, active LOW |

## I2C Bus Topology

### Bus 0 (pins 21/22) — 400 kHz
```
SDA (21) ──┬── MPU6050 (0x68)
           └── SSD1306  (0x3C)
SCL (22) ──┬── MPU6050
           └── SSD1306
```

Internal INPUT_PULLUP enabled during init. Both devices 3.3V tolerant.

### Bus 1 (pins 18/23) — 100 kHz
```
SDA (18) ── DS3231 (0x68)
SCL (23) ── DS3231
```

Dedicated bus for RTC. Avoids address conflict with MPU6050 (both use 0x68).

## Wokwi Simulation Pins

When `WOKWI_SIM` defined, HX711 pins remap:
| Physical | Wokwi |
|----------|-------|
| 16 (DT) | 25 |
| 17 (SCK) | 26 |
| 36 (DRDY) | 34 |

## Driver Summary

| Driver | Interface | Type | Key Functions |
|--------|-----------|------|---------------|
| HX711Driver | GPIO bit-bang | class | readRaw(), tare(), powerDown() |
| MPU6050Driver | I2C | class | readAccel(), readGyro(), calibrate() |
| SSD1306Driver | I2C | class | drawPixel, print, display(), sleep() |
| Lcd1602Driver | I2C (PCF8574) | class | init(), print(), clear() |
| FingerprintDriver | UART2 | class (wraps Adafruit) | startScan(), checkScan(), startEnroll() |
| RtcDriver | I2C_NUM_1 | free functions | rtc_init(), rtc_now(), rtc_isAvailable() |
| InterruptManager | GPIO ISR | static class | begin(), isHX711Ready(), isMPUTriggered() |

## LED Signaling

| Pattern | Meaning |
|---------|---------|
| Solid ON | AP mode active |
| 3s blink | STA connected, all OK |
| 200ms blink | STA connected, errors/warnings |
| 500ms blink | STA disconnected |

LED is active LOW (GPIO2 LOW = LED on).

## Safe-Fail Design

- **Relay**: active LOW. Power loss = de-energized = door locked.
- **Button hold at boot**: wipes WiFi creds, forces AP mode (recovery).
- **No pull resistors needed**: INPUT_PULLUP on button, reed switch, I2C pins.

# Citations

[1] docs/hardware.md — Full hardware documentation
[2] src/config/Config.h — Pin definitions
