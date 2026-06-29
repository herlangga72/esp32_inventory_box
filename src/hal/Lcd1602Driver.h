#ifndef LCD1602_DRIVER_H
#define LCD1602_DRIVER_H

#include <stdint.h>

class String;

// ponytail: single I2C address (0x27). If your backpack uses 0x3F, config change below.
#define LCD1602_ADDR     0x27
#define LCD1602_COLS     16
#define LCD1602_ROWS     2

class Lcd1602Driver {
public:
    Lcd1602Driver();
    ~Lcd1602Driver();

    bool init(uint8_t address = LCD1602_ADDR);
    void clear();
    void setCursor(uint8_t col, uint8_t row);
    void setBacklight(bool on);

    void print(const char* str);
    void print(const String& str);
    void print(float val, int decimals = 1);
    void print(int val);

    bool isInitialized() { return initialized; }

private:
    void* lcd;  // opaque — LiquidCrystal_I2C allocated internally
    uint8_t address;
    bool initialized;
};

#endif // LCD1602_DRIVER_H
