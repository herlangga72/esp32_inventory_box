# ESP32 Inventory Box — Documentation

Smart inventory tracking system: weight (HX711 load cell) + motion (MPU6050 accelerometer) + fingerprint access (R307). ESP32, Arduino framework, PlatformIO.

## Docs Index

| File | Covers |
|------|--------|
| [architecture.md](architecture.md) | Microkernel design, ServiceRegistry, memory pools, mailbox IPC, layer breakdown |
| [services.md](services.md) | Domain services: WeightService, MotionService, MatchingService, StateManager, AccessController, DoorService |
| [data-layer.md](data-layer.md) | StorageManager (NVS), ToolRepository, UserRepository, LogRepository, serialization |
| [tasks-and-timing.md](tasks-and-timing.md) | FreeRTOS tasks, priorities, dynamic rates, WiFi states, PowerManager, boot sequence |
| [web-api.md](web-api.md) | Complete REST API reference with request/response schemas |
| [hardware.md](hardware.md) | Pin map, sensor specs, wiring, I2C bus, UART, relay, reed switch |
| [development.md](development.md) | Build commands, code patterns, conventions, debugging |

## Quick Start

```bash
pio run                    # Build firmware
pio run --target upload    # Flash to ESP32
pio run --target buildfs   # Build SPIFFS (web UI)
pio run --target uploadfs  # Upload SPIFFS
pio device monitor         # Serial monitor (115200 baud)
```

## System at a Glance

- **MCU**: ESP32 (unicore FreeRTOS, 1 kHz tick, tickless idle)
- **Sensors**: HX711 24-bit ADC (load cell), MPU6050 6-axis IMU, R307 fingerprint
- **Outputs**: SSD1306 128x64 OLED, relay (solenoid door lock), status LED
- **Inputs**: reed switch (door sensor), BOOT button
- **Connectivity**: WiFi STA + AP fallback, mDNS, OTA (debug builds)
- **Storage**: NVS for config/entities, SPIFFS for web UI + logs
- **Power**: dynamic frequency scaling (80-240 MHz), light/deep sleep
