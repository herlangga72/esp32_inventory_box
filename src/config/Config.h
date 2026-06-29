#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#ifdef WOKWI_SIM
// ---- Wokwi pin mapping (wokwi-esp32-devkit-v1 exposes D2-D35 only) ----
#define PIN_HX711_DT     25    // D25 (physical: 16, not exposed on Wokwi)
#define PIN_HX711_SCK    26    // D26 (physical: 17, not exposed on Wokwi)
#define PIN_HX711_DRDY   34    // D34 (Wokwi HX711 model lacks DRDY — interrupt never fires)
#else
// ---- Physical hardware pin mapping ----
#define PIN_HX711_DT     16
#define PIN_HX711_SCK    17
#define PIN_HX711_DRDY   36
#endif

// MPU-6050 Motion Sensor
#define PIN_MPU_INT      35    // D35 available on both
#define PIN_MPU_SDA      21    // D21 available on both
#define PIN_MPU_SCL      22    // D22 available on both
#define MPU6050_ADDR     0x68

// SSD1306 OLED Display (I2C)
#define PIN_DISPLAY_SDA  21    // D21 (shared I2C bus)
#define PIN_DISPLAY_SCL  22    // D22
#define PIN_DISPLAY_RST  19    // D19 (OLED reset)
#define DISPLAY_ADDR     0x3C

// LCD 16x2 with PCF8574 I2C Backpack — shares SDA/SCL with MPU6050+SSD1306
#define LCD1602_ADDR_1   0x27  // Most common backpack address
#define LCD1602_ADDR_2   0x3F  // Alternate (some PCF8574 modules)

// Button
#define PIN_BUTTON       33    // D33

// Status LED (built-in blue LED on most ESP32 dev boards, active LOW)
#define PIN_LED          2     // D2

// Fingerprint Sensor (R307/AS608 via UART2, remapped)
#define PIN_FP_RX         5    // D5
#define PIN_FP_TX         4    // D4
#define FP_BAUDRATE       57600

// Relay (solenoid door lock)
#define PIN_RELAY         13    // D13
#define RELAY_ACTIVE_STATE LOW   // LOW = relay energized = door unlocked (safe-fail)
#define RELAY_DURATION_MS 5000   // pulse duration in ms

// Door Sensor (reed switch)
#define PIN_DOOR_SENSOR   14    // D14, INPUT_PULLUP, LOW = door closed, HIGH = door open

// Server communication
#define SERVER_TIMEOUT_MS       5000
#define FP_SCAN_INTERVAL_MS     200
#define FP_ENROLL_TIMEOUT_MS    30000

// DS3231 RTC Module
#define PIN_RTC_SDA      18    // D18 (I2C_NUM_1, separate bus from MPU6050+SSD1306)
#define PIN_RTC_SCL      23    // D23
#define RTC_ADDR         0x68
#define RTC_I2C_NUM      I2C_NUM_1
#define RTC_I2C_FREQUENCY 100000

// I2C Settings
#define I2C_FREQUENCY    400000

namespace Config {
    // System Constants
    constexpr float WEIGHT_THRESHOLD_GRAMS     = 2.0f;
    constexpr int   SETTLING_TIME_MS           = 3000;
    constexpr int   IDLE_TIMEOUT_MS            = 30000;
    constexpr int   DEEP_SLEEP_TIMEOUT_MS      = 60000;
    constexpr float MOTION_THRESHOLD_G         = 0.15f;
    constexpr float TILT_THRESHOLD_G           = 0.7f;
    constexpr float FREE_FALL_THRESHOLD_G      = 0.16f;
    constexpr int   MAX_CONTENTS               = 10;

    // Filter Settings
    constexpr int   FILTER_SIZE                = 10;
    constexpr int   HX711_READ_INTERVAL_MS     = 100;

    // Capacity
    constexpr int   MAX_TOOLS                  = 20;
    constexpr int   MAX_USERS                  = 50;
    constexpr int   MAX_LOGS                   = 500;
    constexpr int   MAX_FINGERPRINTS           = 128;  // R307 capacity

    // Logging
    constexpr int   LOG_QUEUE_DEPTH            = 128;
    constexpr int   DEFAULT_LOG_LEVEL          = 3;  // LOG_INFO

    // Defaults
    constexpr float DEFAULT_TOLERANCE          = 5.0f;
    constexpr float CALIBRATION_FACTOR         = -471.0f;  // Adjust for your load cell
    constexpr const char* NVS_NAMESPACE        = "inventory_box";
}

// Operational mode — drives task rates and power management
enum class OperationalMode {
    OP_AP_FULL,   // AP active — full speed, no power saving
    OP_STA_IDLE   // STA connected — slow tasks, power saving enabled
};

// Task delay presets (ms)
namespace TaskRate {
    // AP mode (full speed)
    constexpr int AP_STATE_MS    = 50;
    constexpr int AP_MOTION_MS   = 10;
    constexpr int AP_WEIGHT_MS   = 100;
    constexpr int AP_ACCESS_MS   = 200;
    constexpr int AP_WIFI_MS     = 10;
    constexpr int AP_WEB_MS      = 1;
    constexpr int AP_DISPLAY_MS  = 1000;

    // STA mode (power save)
    constexpr int STA_STATE_MS   = 200;
    constexpr int STA_MOTION_MS  = 100;
    constexpr int STA_WEIGHT_MS  = 500;
    constexpr int STA_ACCESS_MS  = 500;
    constexpr int STA_WIFI_MS    = 100;
    constexpr int STA_WEB_MS     = 50;
    constexpr int STA_DISPLAY_MS = 5000;
}

#endif // CONFIG_H
