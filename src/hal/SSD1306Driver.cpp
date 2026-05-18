#include "SSD1306Driver.h"

SSD1306Driver::SSD1306Driver(int sda, int scl, int rst) 
    : sda(sda), scl(scl), rst(rst), address(DISPLAY_ADDR), sleeping(false),
      initialized(false), cursorX(0), cursorY(0), textSize(1), textColor(1) {
    memset(buffer, 0, BUFFER_SIZE);
}

bool SSD1306Driver::init(uint8_t address) {
    this->address = address;
    
    Wire.begin(sda, scl);
    Wire.setClock(400000);
    
    // Hardware reset if RST pin defined
    if (rst >= 0) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, HIGH);
        delay(10);
        digitalWrite(rst, LOW);
        delay(50);
        digitalWrite(rst, HIGH);
        delay(50);
    }
    
    // Init sequence
    sendCommand(0xAE);  // Display off
    sendCommand(0xD5);  // Clock div
    sendCommand(0x80);
    sendCommand(0xA8);  // Mux ratio
    sendCommand(0x3F);
    sendCommand(0xD3);  // Display offset
    sendCommand(0x00);
    sendCommand(0x40);  // Start line
    sendCommand(0xA1);  // Segment remap
    sendCommand(0xC8);  // COM scan direction
    sendCommand(0xDA);  // COM pins
    sendCommand(0x12);
    sendCommand(0x81);  // Contrast
    sendCommand(0xCF);
    sendCommand(0xD9);  // Pre-charge
    sendCommand(0xF1);
    sendCommand(0xDB);  // VCOM deselect
    sendCommand(0x40);
    sendCommand(0x8D);  // Charge pump
    sendCommand(0x14);
    sendCommand(0xA4);  // RAM display
    sendCommand(0xA6);  // Normal display
    sendCommand(0xAF);  // Display on
    
    clear();
    display();
    initialized = true;
    
    return true;
}

void SSD1306Driver::clear() {
    memset(buffer, 0, BUFFER_SIZE);
    cursorX = cursorY = 0;
}

void SSD1306Driver::display() {
    // Page addressing mode
    sendCommand(0x00);  // Lower column start
    sendCommand(0x10);  // Higher column start
    sendCommand(0xB0);  // Page start
    
    // Send buffer pages
    for (int page = 0; page < 8; page++) {
        sendCommand(0xB0 + page);
        sendCommand(0x00);
        sendCommand(0x10);
        
        sendBuffer(&buffer[page * 128], 128);
    }
}

void SSD1306Driver::setTextSize(uint8_t size) {
    textSize = size > 0 ? size : 1;
}

void SSD1306Driver::setCursor(uint8_t x, uint8_t y) {
    cursorX = x;
    cursorY = y;
}

void SSD1306Driver::setTextColor(uint16_t color) {
    textColor = color;
}

void SSD1306Driver::fillScreen(uint16_t color) {
    memset(buffer, color ? 0xFF : 0x00, BUFFER_SIZE);
}

void SSD1306Driver::print(const char* str) {
    while (*str) {
        writeChar(*str++);
    }
}

void SSD1306Driver::print(const String& str) {
    print(str.c_str());
}

void SSD1306Driver::print(float val, int decimals) {
    char buf[16];
    dtostrf(val, 0, decimals, buf);
    print(buf);
}

void SSD1306Driver::print(int val) {
    char buf[12];
    itoa(val, buf, 10);
    print(buf);
}

void SSD1306Driver::println(const char* str) {
    print(str);
    println();
}

void SSD1306Driver::println(const String& str) {
    print(str);
    println();
}

void SSD1306Driver::println() {
    cursorY += 8 * textSize;
    cursorX = 0;
}

void SSD1306Driver::drawPixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    
    int page = y / 8;
    int bit = y % 8;
    
    if (color) {
        buffer[page * 128 + x] |= (1 << bit);
    } else {
        buffer[page * 128 + x] &= ~(1 << bit);
    }
}

void SSD1306Driver::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    // Bresenham's algorithm
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void SSD1306Driver::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
    drawLine(x + w - 1, y + h - 1, x, y + h - 1, color);
    drawLine(x, y + h - 1, x, y, color);
}

void SSD1306Driver::fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            drawPixel(x + i, y + j, color);
        }
    }
}

void SSD1306Driver::sleep() {
    sendCommand(0xAE);  // Display off
    sleeping = true;
}

void SSD1306Driver::wake() {
    sendCommand(0xAF);  // Display on
    sleeping = false;
}

void SSD1306Driver::writeChar(uint8_t c) {
    if (c == '\n') {
        cursorY += 8 * textSize;
        cursorX = 0;
        return;
    }
    
    if (c == '\r') return;
    
    // Simple 5x7 font (basic ASCII)
    static const uint8_t font[][5] = {
        {0x00,0x00,0x00,0x00,0x00}, // space
        {0x00,0x00,0x5F,0x00,0x00}, // !
        {0x00,0x07,0x00,0x07,0x00}, // "
        {0x14,0x7F,0x14,0x7F,0x14}, // #
        {0x24,0x2E,0x6B,0x6B,0x12}, // $
        // ... simplified for brevity, use Adafruit_GFX in real project
    };
    
    // For now just advance cursor
    cursorX += 6 * textSize;
    if (cursorX > WIDTH) {
        cursorX = 0;
        cursorY += 8 * textSize;
    }
}

void SSD1306Driver::sendCommand(uint8_t cmd) {
    Wire.beginTransmission(address);
    Wire.write(0x80);  // Command mode
    Wire.write(cmd);
    Wire.endTransmission();
}

void SSD1306Driver::sendData(uint8_t data) {
    Wire.beginTransmission(address);
    Wire.write(0x40);  // Data mode
    Wire.write(data);
    Wire.endTransmission();
}

void SSD1306Driver::sendBuffer(const uint8_t* data, size_t len) {
    Wire.beginTransmission(address);
    Wire.write(0x40);
    for (size_t i = 0; i < len; i++) {
        Wire.write(data[i]);
    }
    Wire.endTransmission();
}

void SSD1306Driver::drawChar(char c) {
    writeChar(c);
}