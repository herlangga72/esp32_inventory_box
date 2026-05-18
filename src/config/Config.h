#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// HX711 Load Cell Amplifier
#define PIN_HX711_DT     16
#define PIN_HX711_SCK    17
#define PIN_HX711_DRDY   36

// MPU-6050 Motion Sensor
#define PIN_MPU_INT      35
#define PIN_MPU_SDA      21
#define PIN_MPU_SCL      22
#define MPU6050_ADDR     0x68

// SSD1306 OLED Display
#define PIN_DISPLAY_SDA  21
#define PIN_DISPLAY_SCL  22
#define PIN_DISPLAY_RST  19
#define DISPLAY_ADDR     0x3C

// Button
#define PIN_BUTTON       33

// System Constants
#define WEIGHT_THRESHOLD_GRAMS     2.0f
#define SETTLING_TIME_MS          3000
#define IDLE_TIMEOUT_MS           30000
#define DEEP_SLEEP_TIMEOUT_MS     60000
#define MOTION_THRESHOLD_G        0.15f
#define TILT_THRESHOLD_G          0.7f
#define FREE_FALL_THRESHOLD_G     0.16f
#define MAX_CONTENTS              10

// Filter Settings
#define FILTER_SIZE               10
#define HX711_READ_INTERVAL_MS    100

// I2C Settings
#define I2C_FREQUENCY             400000

namespace Config {
    extern const char* NVS_NAMESPACE;
    extern const int MAX_TOOLS;
    extern const int MAX_USERS;
    extern const int MAX_LOGS;
    extern const float DEFAULT_TOLERANCE;
    extern const float CALIBRATION_FACTOR;
}

#endif // CONFIG_H