#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include <Arduino.h>

class HX711Driver {
public:
    HX711Driver(int dataPin = PIN_HX711_DT, int clockPin = PIN_HX711_SCK, int drdyPin = PIN_HX711_DRDY);
    void begin();
    int32_t readRaw();                  // Blocking read, returns 24-bit signed value
    void setGain(int gain);             // 128 (default) or 64
    void powerDown();                   // Enter power down mode
    void powerUp();                     // Exit power down mode
    bool isReady();                     // Check if data is ready
    void tare(int samples = 10);       // Tare (zero) the scale
    int32_t getOffset() { return offset; }
    void setOffset(int32_t val) { offset = val; }

private:
    int pinData;
    int pinClock;
    int pinDRDY;
    int gain;
    int32_t offset;
    bool initialized;

    int32_t readBit();                 // Read single bit
    int32_t readByte();                // Read single byte
};

#endif // HX711_DRIVER_H