#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "../kernel/ServiceRegistry.h"

class SSD1306Driver;
class Lcd1602Driver;

enum class Screen : uint8_t {
    STATUS,
    EVENT_LOG,
    SETTINGS,
    CALIBRATION,
    ERROR
};

void dm_dispatchMessage(DisplayManagerMemory* mem, const ServiceMessage& msg);
void dm_update(DisplayManagerMemory* mem, SSD1306Driver* oled, Lcd1602Driver* lcd);

#endif // DISPLAY_MANAGER_H
