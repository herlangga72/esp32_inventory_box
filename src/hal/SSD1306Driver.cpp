#include "SSD1306Driver.h"
#include "InterruptManager.h"

SSD1306Driver::SSD1306Driver(int sda, int scl, int rst) 
    : sda(sda), scl(scl), rst(rst), address(DISPLAY_ADDR), sleeping(false),
      initialized(false), cursorX(0), cursorY(0), textSize(1), textColor(1) {
    memset(buffer, 0, BUFFER_SIZE);
}

bool SSD1306Driver::init(uint8_t address) {
    this->address = address;
    i2cLock();

    Wire.begin(sda, scl);
    Wire.setClock(400000);
    Wire.setTimeOut(50);

    // Check if device is present on I2C bus
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
        Wire.end();
        initialized = false;
        i2cUnlock();
        return false;
    }

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
    i2cUnlock();
    return true;
}

bool SSD1306Driver::ping() {
    i2cLock();
    Wire.beginTransmission(address);
    uint8_t err = Wire.endTransmission();
    i2cUnlock();
    return (err == 0);
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

    // 5x7 font — 95 printable ASCII (32–126)
    static const uint8_t font[][5] PROGMEM = {
        {0x00,0x00,0x00,0x00,0x00}, // 32  space
        {0x00,0x00,0x5F,0x00,0x00}, // 33  !
        {0x00,0x07,0x00,0x07,0x00}, // 34  "
        {0x14,0x7F,0x14,0x7F,0x14}, // 35  #
        {0x24,0x2E,0x6B,0x6B,0x12}, // 36  $
        {0x23,0x13,0x08,0x64,0x62}, // 37  %
        {0x36,0x49,0x55,0x22,0x50}, // 38  &
        {0x00,0x05,0x03,0x00,0x00}, // 39  '
        {0x00,0x1C,0x22,0x41,0x00}, // 40  (
        {0x00,0x41,0x22,0x1C,0x00}, // 41  )
        {0x08,0x2A,0x1C,0x2A,0x08}, // 42  *
        {0x08,0x08,0x3E,0x08,0x08}, // 43  +
        {0x00,0x50,0x30,0x00,0x00}, // 44  ,
        {0x08,0x08,0x08,0x08,0x08}, // 45  -
        {0x00,0x60,0x60,0x00,0x00}, // 46  .
        {0x20,0x10,0x08,0x04,0x02}, // 47  /
        {0x3E,0x51,0x49,0x45,0x3E}, // 48  0
        {0x00,0x42,0x7F,0x40,0x00}, // 49  1
        {0x42,0x61,0x51,0x49,0x46}, // 50  2
        {0x21,0x41,0x45,0x4B,0x31}, // 51  3
        {0x18,0x14,0x12,0x7F,0x10}, // 52  4
        {0x27,0x45,0x45,0x45,0x39}, // 53  5
        {0x3C,0x4A,0x49,0x49,0x30}, // 54  6
        {0x01,0x71,0x09,0x05,0x03}, // 55  7
        {0x36,0x49,0x49,0x49,0x36}, // 56  8
        {0x06,0x49,0x49,0x29,0x1E}, // 57  9
        {0x00,0x36,0x36,0x00,0x00}, // 58  :
        {0x00,0x56,0x36,0x00,0x00}, // 59  ;
        {0x00,0x08,0x14,0x22,0x41}, // 60  <
        {0x14,0x14,0x14,0x14,0x14}, // 61  =
        {0x41,0x22,0x14,0x08,0x00}, // 62  >
        {0x02,0x01,0x51,0x09,0x06}, // 63  ?
        {0x32,0x49,0x79,0x41,0x3E}, // 64  @
        {0x7E,0x11,0x11,0x11,0x7E}, // 65  A
        {0x7F,0x49,0x49,0x49,0x36}, // 66  B
        {0x3E,0x41,0x41,0x41,0x22}, // 67  C
        {0x7F,0x41,0x41,0x22,0x1C}, // 68  D
        {0x7F,0x49,0x49,0x49,0x41}, // 69  E
        {0x7F,0x09,0x09,0x01,0x01}, // 70  F
        {0x3E,0x41,0x41,0x51,0x32}, // 71  G
        {0x7F,0x08,0x08,0x08,0x7F}, // 72  H
        {0x00,0x41,0x7F,0x41,0x00}, // 73  I
        {0x20,0x40,0x41,0x3F,0x01}, // 74  J
        {0x7F,0x08,0x14,0x22,0x41}, // 75  K
        {0x7F,0x40,0x40,0x40,0x40}, // 76  L
        {0x7F,0x02,0x04,0x02,0x7F}, // 77  M
        {0x7F,0x04,0x08,0x10,0x7F}, // 78  N
        {0x3E,0x41,0x41,0x41,0x3E}, // 79  O
        {0x7F,0x09,0x09,0x09,0x06}, // 80  P
        {0x3E,0x41,0x51,0x21,0x5E}, // 81  Q
        {0x7F,0x09,0x19,0x29,0x46}, // 82  R
        {0x46,0x49,0x49,0x49,0x31}, // 83  S
        {0x01,0x01,0x7F,0x01,0x01}, // 84  T
        {0x3F,0x40,0x40,0x40,0x3F}, // 85  U
        {0x1F,0x20,0x40,0x20,0x1F}, // 86  V
        {0x7F,0x20,0x18,0x20,0x7F}, // 87  W
        {0x63,0x14,0x08,0x14,0x63}, // 88  X
        {0x03,0x04,0x78,0x04,0x03}, // 89  Y
        {0x61,0x51,0x49,0x45,0x43}, // 90  Z
        {0x00,0x00,0x7F,0x41,0x41}, // 91  [
        {0x02,0x04,0x08,0x10,0x20}, // 92  backslash
        {0x41,0x41,0x7F,0x00,0x00}, // 93  ]
        {0x04,0x02,0x01,0x02,0x04}, // 94  ^
        {0x40,0x40,0x40,0x40,0x40}, // 95  _
        {0x00,0x01,0x02,0x04,0x00}, // 96  `
        {0x20,0x54,0x54,0x54,0x78}, // 97  a
        {0x7F,0x48,0x44,0x44,0x38}, // 98  b
        {0x38,0x44,0x44,0x44,0x20}, // 99  c
        {0x38,0x44,0x44,0x48,0x7F}, // 100 d
        {0x38,0x54,0x54,0x54,0x18}, // 101 e
        {0x08,0x7E,0x09,0x01,0x02}, // 102 f
        {0x08,0x14,0x54,0x54,0x3C}, // 103 g
        {0x7F,0x08,0x04,0x04,0x78}, // 104 h
        {0x00,0x44,0x7D,0x40,0x00}, // 105 i
        {0x20,0x40,0x44,0x3D,0x00}, // 106 j
        {0x00,0x7F,0x10,0x28,0x44}, // 107 k
        {0x00,0x41,0x7F,0x40,0x00}, // 108 l
        {0x7C,0x04,0x18,0x04,0x78}, // 109 m
        {0x7C,0x08,0x04,0x04,0x78}, // 110 n
        {0x38,0x44,0x44,0x44,0x38}, // 111 o
        {0x7C,0x14,0x14,0x14,0x08}, // 112 p
        {0x08,0x14,0x14,0x18,0x7C}, // 113 q
        {0x7C,0x08,0x04,0x04,0x08}, // 114 r
        {0x48,0x54,0x54,0x54,0x20}, // 115 s
        {0x04,0x3F,0x44,0x40,0x20}, // 116 t
        {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
        {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
        {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
        {0x44,0x28,0x10,0x28,0x44}, // 120 x
        {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
        {0x44,0x64,0x54,0x4C,0x44}, // 122 z
        {0x00,0x08,0x36,0x41,0x00}, // 123 {
        {0x00,0x00,0x7F,0x00,0x00}, // 124 |
        {0x00,0x41,0x36,0x08,0x00}, // 125 }
        {0x08,0x08,0x2A,0x1C,0x08}, // 126 ~
    };

    if (c < 32 || c > 126) return;

    const uint8_t* glyph = font[c - 32];

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < 8; row++) {
            if (line & (1 << row)) {
                drawPixel(cursorX + col, cursorY + row, textColor);
            }
        }
    }

    cursorX += 6 * textSize;
    if (cursorX + 6 > WIDTH) {
        cursorX = 0;
        cursorY += 8 * textSize;
    }
}

void SSD1306Driver::sendCommand(uint8_t cmd) {
    i2cLock();
    Wire.beginTransmission(address);
    Wire.write(0x80);  // Command mode
    Wire.write(cmd);
    Wire.endTransmission();
    i2cUnlock();
}

void SSD1306Driver::sendData(uint8_t data) {
    i2cLock();
    Wire.beginTransmission(address);
    Wire.write(0x40);  // Data mode
    Wire.write(data);
    Wire.endTransmission();
    i2cUnlock();
}

void SSD1306Driver::sendBuffer(const uint8_t* data, size_t len) {
    i2cLock();
    Wire.beginTransmission(address);
    Wire.write(0x40);
    for (size_t i = 0; i < len; i++) {
        Wire.write(data[i]);
    }
    Wire.endTransmission();
    i2cUnlock();
}

void SSD1306Driver::drawChar(char c) {
    writeChar(c);
}