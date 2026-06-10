#ifndef SSD1306_DRIVER_H
#define SSD1306_DRIVER_H

#include <stdint.h>
#include <stddef.h>

// Arduino String forward compat
class String;
#define DISPLAY_ADDR 0x3C

// Pin defaults (from Config.h macros if available, else hardcoded)
#ifndef PIN_DISPLAY_SDA
#define PIN_DISPLAY_SDA 21
#endif
#ifndef PIN_DISPLAY_SCL
#define PIN_DISPLAY_SCL 22
#endif
#ifndef PIN_DISPLAY_RST
#define PIN_DISPLAY_RST 19
#endif

class SSD1306Driver {
public:
    SSD1306Driver(int sda = PIN_DISPLAY_SDA, int scl = PIN_DISPLAY_SCL,
                  int rst = PIN_DISPLAY_RST);

    bool init(uint8_t address = DISPLAY_ADDR);
    bool ping();
    void clear();
    void display();

    void setTextSize(uint8_t size);
    void setCursor(uint8_t x, uint8_t y);
    void setTextColor(uint16_t color);
    void fillScreen(uint16_t color);

    void print(const char* str);
    void print(const String& str);
    void print(float val, int decimals = 1);
    void print(int val);
    void println(const char* str);
    void println(const String& str);
    void println();

    void drawPixel(int x, int y, uint16_t color = 1);
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color = 1);
    void drawRect(int x, int y, int w, int h, uint16_t color = 1);
    void fillRect(int x, int y, int w, int h, uint16_t color = 1);

    void sleep();
    void wake();
    bool isAwake() { return !sleeping; }
    bool isInitialized() { return initialized; }

private:
    int sda, scl, rst;
    uint8_t address;
    bool sleeping;
    bool initialized;

    static const int WIDTH = 128;
    static const int HEIGHT = 64;
    static const int BUFFER_SIZE = 1024;
    uint8_t buffer[BUFFER_SIZE];

    uint8_t cursorX, cursorY;
    uint8_t textSize;
    uint16_t textColor;

    void sendCommand(uint8_t cmd);
    void sendBuffer(const uint8_t* data, size_t len);
    void writeChar(uint8_t c);
};

#endif // SSD1306_DRIVER_H
