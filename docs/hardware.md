# Hardware Reference

## Pin Map

| Pin | Function | Type | Notes |
|-----|----------|------|-------|
| **Weight** |
| 16 | HX711 DT | Input | Load cell data |
| 17 | HX711 SCK | Output | Load cell clock |
| 36 | HX711 DRDY | Input | Data ready interrupt (ADC input only, no pull) |
| **Motion** |
| 21 | MPU6050 SDA | I2C | Shared with SSD1306 |
| 22 | MPU6050 SCL | I2C | Shared with SSD1306 |
| 35 | MPU INT | Input | Motion interrupt |
| **Display** |
| 21 | SSD1306 SDA | I2C | Shared with MPU6050 |
| 22 | SSD1306 SCL | I2C | Shared with MPU6050 |
| 19 | OLED RST | Output | Display reset |
| **Fingerprint** |
| 5 | FP RX | UART RX | Remapped from default Serial2 |
| 4 | FP TX | UART TX | Remapped from default Serial2 |
| **Door** |
| 13 | Relay | Output | Solenoid lock, active LOW |
| 14 | Door Sensor | Input | Reed switch, INPUT_PULLUP, LOW=closed |
| **System** |
| 33 | Button | Input | BOOT button, INPUT_PULLUP, LOW=pressed |
| 2 | LED | Output | Built-in blue LED, active LOW |

## I2C Bus

- **Frequency**: 400 kHz (fast mode)
- **Shared bus**: MPU6050 + SSD1306 on same SDA/SCL
- **Pullups**: internal `INPUT_PULLUP` enabled on both pins during init
- **Addresses**:
  - MPU6050: 0x68
  - SSD1306: 0x3C

Both devices are 3.3V tolerant. No level shifting needed.

## HX711 — Load Cell Amplifier

24-bit ADC with programmable gain (128 or 64).

**Wiring**:
```
HX711     ESP32
VCC  →   3.3V
GND  →   GND
DT   →   GPIO16
SCK  →   GPIO17
DRDY →   GPIO36 (optional — interrupt-driven reading)
```

**Timing**:
- Sample rate: 10 Hz (80 Hz raw capability, but filtered)
- Read interval: 100ms minimum (Config::HX711_READ_INTERVAL_MS)
- DRDY goes LOW when data ready → ISR signals WeightService

**Calibration factor**: `-471.0` default (Config::CALIBRATION_FACTOR). Adjust per load cell.

**Driver class**: `HX711Driver`
- `begin()` — initialize pins
- `readRaw()` — returns 24-bit signed value (INT32_MIN on error)
- `setGain(128)` or `setGain(64)`
- `tare(samples)` — auto-zero with averaging
- `powerDown()` / `powerUp()` — power saving

## MPU6050 — 6-Axis IMU

Accelerometer + gyroscope. Used for motion detection (not orientation).

**Address**: 0x68 (AD0 pin LOW)

**Settings**:
- Accel range: ±2g (default)
- DLPF: 5 Hz bandwidth for stable resting detection
- Motion interrupt: enabled, threshold 0.5g
- Sample rate: 100 Hz

**Driver class**: `MPU6050Driver`
- `begin()` — I2C init, configure ranges, enable motion interrupt
- `readAccel(ax, ay, az)` — read acceleration in g
- `readGyro(gx, gy, gz)` — read angular velocity in °/s
- `enableMotionInterrupt(enable, threshold)` — configure INT pin
- `calibrate(axOff, ayOff, azOff)` — compute offsets

**Motion classification thresholds** (Config.h):
- MOTION_THRESHOLD: 0.15g (minimal movement)
- TILT_THRESHOLD: 0.7g (Z-axis change = lid open)
- FREE_FALL_THRESHOLD: 0.16g (total magnitude below = free fall)

## R307 / AS608 — Fingerprint Sensor

Optical fingerprint sensor. UART protocol at 57600 baud.

**Wiring**:
```
R307      ESP32
VCC  →   3.3V (or 5V if module supports it)
GND  →   GND
TX   →   GPIO5 (ESP32 RX)
RX   →   GPIO4 (ESP32 TX)
```

**UART**: HardwareSerial2, remapped via `Serial2.begin(57600, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX)`

**Driver class**: `FingerprintDriver` (wraps Adafruit_Fingerprint)
- `begin()` — UART init, sensor handshake, password verification
- `startScan()` — arm sensor to look for finger (non-blocking)
- `checkScan()` — returns -1 (no finger), -2 (no match), >=0 (finger ID)
- `startEnroll(id)` — begin multi-step enrollment
- `checkEnrollStep()` — returns -2 (fail), -1 (timeout), 0 (need image1), 1 (need image2), 2 (success)
- `cancelEnroll()` — abort enrollment
- `deleteFingerprint(id)` — delete single template
- `deleteAll()` — clear all templates
- `getTemplateCount()` — number of stored templates

**Capacity**: 128 templates (Config::MAX_FINGERPRINTS)

**Timing**:
- Scan interval: 200ms (FP_SCAN_INTERVAL_MS)
- Enrollment timeout: 30s (FP_ENROLL_TIMEOUT_MS)

## SSD1306 — OLED Display

128x64 monochrome OLED. I2C interface.

**Address**: 0x3C

**Buffer**: 1024 bytes (128 * 64 / 8 bits) in driver memory.

**Driver class**: `SSD1306Driver`
- `init(address)` — I2C init, send init sequence
- `ping()` — I2C probe, returns true if device ACKs
- `clear()` — clear buffer
- `display()` — flush buffer to OLED
- `sleep()` / `wake()` — display power control
- `isInitialized()` / `isAwake()` — status queries
- Drawing: `drawPixel`, `drawLine`, `drawRect`, `fillRect`
- Text: `setCursor(x,y)`, `setTextSize(n)`, `print(str/int/float)`

**Health monitoring**: DisplayManager pings every 5s. On failure → reinit attempt. On recovery → resume.

**Screen states** (DisplayManager):
- STATUS — weight, baseline, delta, contents count, user, WiFi RSSI
- EVENT_LOG — recent events (placeholder)
- SETTINGS — menu options
- CALIBRATION — progress bar
- ERROR — error message
- Notification overlay — 3s temporary banner at bottom

## Relay — Solenoid Door Lock

**Pin**: GPIO13
**Active state**: LOW (energized = unlocked)
**Safe-fail**: HIGH (de-energized = locked) — power loss = door stays locked

**Timing**:
- Default unlock duration: 5000ms (RELAY_DURATION_MS)
- Settable via DoorService SET_DURATION command
- Auto-lock timer in DoorService::ds_update()

## Reed Switch — Door Sensor

**Pin**: GPIO14
**Mode**: INPUT_PULLUP
**Logic**: LOW = magnet near = door closed, HIGH = door open
**Debounce**: 3-sample majority vote (10ms between samples)

## Button

**Pin**: GPIO33
**Mode**: INPUT_PULLUP
**Function**: BOOT button. Hold during power-on → force AP mode (wipes WiFi creds).

## Status LED

**Pin**: GPIO2 (built-in blue LED on most ESP32 dev boards)
**Active**: LOW (LED on when pin pulled LOW)

LED patterns:
- Boot: 3 fast blinks → 2s solid on
- AP mode: solid on
- STA connected + all OK: 3s slow blink
- STA connected + errors: 200ms fast blink
- STA disconnected: 500ms medium blink

## Power Supply Considerations

- ESP32: 3.3V, ~80mA typical (240mA with WiFi TX)
- HX711 + load cell: 3.3V, ~1.5mA
- MPU6050: 3.3V, ~3.9mA
- SSD1306: 3.3V, ~20mA (all pixels on)
- R307 fingerprint: 3.3V/5V, ~60mA (during scan)
- Relay: 5V, ~70mA (coil) — separate 5V supply recommended
- **Total peak**: ~450mA — 1A 5V supply with 3.3V LDO recommended
