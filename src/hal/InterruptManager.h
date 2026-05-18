#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#include <Arduino.h>
#include "driver/gpio.h"

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
    static void hx711ISR();
    static void mpuISR();
    static void buttonISR();
    
    static uint32_t lastButtonPress;
    static const uint32_t DEBOUNCE_MS = 50;
};

#endif // INTERRUPT_MANAGER_H