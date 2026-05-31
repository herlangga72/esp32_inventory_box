#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "driver/gpio.h"

// I2C bus mutex — shared by MPU6050 + SSD1306 (same SDA/SCL)
extern SemaphoreHandle_t i2cMutex;
void i2cLock();
void i2cUnlock();

// SPIFFS mutex — protects concurrent read/write across tasks
extern SemaphoreHandle_t spiffsMutex;
void spiffsLock();
void spiffsUnlock();

class InterruptManager {
public:
    static void begin();

    // ISR flags - check and clear
    static bool isHX711Ready();
    static bool isMPUTriggered();
    static bool isButtonPressed();

    // Internal state access
    static volatile bool hx711Flag;
    static volatile bool mpuFlag;
    static volatile bool buttonFlag;

private:
    static void IRAM_ATTR hx711ISR(void* arg);
    static void IRAM_ATTR mpuISR(void* arg);
    static void IRAM_ATTR buttonISR(void* arg);

    static uint32_t lastButtonPress;
    static const uint32_t DEBOUNCE_MS = 50;
};

#endif // INTERRUPT_MANAGER_H