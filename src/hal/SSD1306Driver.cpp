#include "SSD1306Driver.h"
#include "InterruptManager.h"

#include <driver/i2c.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>

// Shared I2C init across SSD1306 + MPU6050
static bool i2cInstalled = false;
#define I2C_MASTER_NUM  I2C_NUM_0
#define I2C_TIMEOUT_MS  50

// Display constants
#define W   128
#define H   64
#define BUF (W * H / 8)  // 1024

SSD1306Driver::SSD1306Driver(int sdaPin, int sclPin, int rstPin)
    : sda(sdaPin), scl(sclPin), rst(rstPin), address(DISPLAY_ADDR),
      sleeping(false), initialized(false),
      cursorX(0), cursorY(0), textSize(1), textColor(1) {
    clear();
}

// ---- I2C master init (idempotent) ----

static bool i2cMasterInit(int sda, int scl) {
    if (i2cInstalled) return true;

    i2c_config_t conf = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = sda;
    conf.scl_io_num       = scl;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    conf.clk_flags        = I2C_SCLK_SRC_FLAG_FOR_NOMAL;

    if (i2c_param_config(I2C_MASTER_NUM, &conf) != ESP_OK) return false;
    esp_err_t err = i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

    i2cInstalled = true;
    return true;
}

// ---- Send single command byte ----

void SSD1306Driver::sendCommand(uint8_t cmd) {
    i2cLock();
    uint8_t buf[2] = {0x00, cmd};
    i2c_master_write_to_device(I2C_MASTER_NUM, address, buf, 2,
                               pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2cUnlock();
}

// ---- Send buffer via I2C cmd link (control byte 0x40 + data) ----

void SSD1306Driver::sendBuffer(const uint8_t* data, size_t len) {
    if (len == 0) return;
    i2cLock();

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);  // data mode
    i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    i2cUnlock();
}

// ---- Init sequence ----

bool SSD1306Driver::init(uint8_t addr) {
    address = addr;
    i2cLock();

    if (!i2cMasterInit(sda, scl)) { i2cUnlock(); return false; }

    // Probe
    i2c_cmd_handle_t probe = i2c_cmd_link_create();
    i2c_master_start(probe);
    i2c_master_write_byte(probe, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(probe);
    esp_err_t probeErr = i2c_master_cmd_begin(I2C_MASTER_NUM, probe,
                                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(probe);
    if (probeErr != ESP_OK) { i2cUnlock(); return false; }

    // Hardware reset
    if (rst >= 0) {
        gpio_config_t rstCfg = {};
        rstCfg.pin_bit_mask = (1ULL << rst);
        rstCfg.mode         = GPIO_MODE_OUTPUT;
        rstCfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        rstCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        rstCfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&rstCfg);

        gpio_set_level((gpio_num_t)rst, 1);  vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)rst, 0);  vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level((gpio_num_t)rst, 1);  vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Init sequence
    static const uint8_t initSeq[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock div
        0xA8, 0x3F,  // mux ratio (64)
        0xD3, 0x00,  // display offset
        0x40,        // start line
        0xA1,        // segment remap (col 127 = SEG0)
        0xC8,        // COM scan direction (remapped)
        0xDA, 0x12,  // COM pins
        0x81, 0xCF,  // contrast
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOM deselect
        0x8D, 0x14,  // charge pump
        0x20, 0x00,  // ** horizontal addressing mode **
        0xA4,        // RAM content display
        0xA6,        // normal (non-inverted)
        0xAF,        // display on
    };
    for (size_t i = 0; i < sizeof(initSeq); i++) {
        sendCommand(initSeq[i]);
    }

    clear();
    display();
    initialized = true;
    i2cUnlock();
    return true;
}

// ---- Ping (I2C probe) ----

bool SSD1306Driver::ping() {
    i2cLock();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    i2cUnlock();
    return (err == ESP_OK);
}

// ---- Buffer operations ----

void SSD1306Driver::clear() {
    // Aligned 32-bit memset: 1024 bytes = 256 × uint32_t
    uint32_t* p = (uint32_t*)buffer;
    for (int i = 0; i < (BUF >> 2); i++) p[i] = 0;
    cursorX = cursorY = 0;
}

void SSD1306Driver::fillScreen(uint16_t color) {
    uint32_t word = color ? 0xFFFFFFFF : 0x00000000;
    uint32_t* p = (uint32_t*)buffer;
    for (int i = 0; i < (BUF >> 2); i++) p[i] = word;
}

// ---- Display: horizontal addressing — single I2C transaction for full frame ----

void SSD1306Driver::display() {
    // Set column range 0–127, page range 0–7 (6 command bytes)
    static const uint8_t addrCmd[] = {
        0x21, 0x00, 0x7F,  // column: 0 → 127
        0x22, 0x00, 0x07,  // page:   0 → 7
    };
    i2cLock();

    // Send address setup + full 1024-byte frame in one I2C transaction
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    // Co = 0 (next byte is command), D/C# = 0 (command)
    for (int i = 0; i < 6; i++) {
        i2c_master_write_byte(cmd, 0x80, true);     // control byte: command
        i2c_master_write_byte(cmd, addrCmd[i], true);
    }
    // Co = 0, D/C# = 1 (data)
    i2c_master_write_byte(cmd, 0x40, true);          // control byte: data
    i2c_master_write(cmd, buffer, BUF, true);        // 1024 bytes in one shot
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

    i2cUnlock();
}

// ---- Text ----

void SSD1306Driver::setTextSize(uint8_t size) { textSize = size > 0 ? size : 1; }
void SSD1306Driver::setCursor(uint8_t x, uint8_t y) { cursorX = x; cursorY = y; }
void SSD1306Driver::setTextColor(uint16_t color) { textColor = color; }

void SSD1306Driver::print(const char* str)          { while (*str) writeChar(*str++); }
void SSD1306Driver::print(const String& str)         { print(str.c_str()); }
void SSD1306Driver::print(float val, int decimals)   { char b[16]; snprintf(b, sizeof(b), "%.*f", decimals, val); print(b); }
void SSD1306Driver::print(int val)                   { char b[12]; snprintf(b, sizeof(b), "%d", val); print(b); }
void SSD1306Driver::println(const char* str)         { print(str); println(); }
void SSD1306Driver::println(const String& str)       { print(str); println(); }

void SSD1306Driver::println() {
    cursorY += (textSize << 3);   // 8 * textSize
    cursorX = 0;
}

// ---- Pixel drawing (bit-shift math) ----

void SSD1306Driver::drawPixel(int x, int y, uint16_t color) {
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    int idx = (x + ((y >> 3) << 7));   // page = y/8, idx = page*128 + x
    uint8_t bit = (uint8_t)(1 << (y & 0x07));
    if (color) buffer[idx] |=  bit;
    else       buffer[idx] &= ~bit;
}

void SSD1306Driver::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) >> 1;
    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

void SSD1306Driver::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
    drawLine(x + w - 1, y + h - 1, x, y + h - 1, color);
    drawLine(x, y + h - 1, x, y, color);
}

void SSD1306Driver::fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int i = 0; i < w; i++)
        for (int j = 0; j < h; j++)
            drawPixel(x + i, y + j, color);
}

void SSD1306Driver::sleep() { sendCommand(0xAE); sleeping = true; }
void SSD1306Driver::wake()  { sendCommand(0xAF); sleeping = false; }

// ---- 5x7 font (95 ASCII, 32–126) ----
// Bit-shift pixel math throughout.

void SSD1306Driver::writeChar(uint8_t c) {
    if (c == '\n') { cursorY += (textSize << 3); cursorX = 0; return; }
    if (c == '\r') return;
    if (c < 32 || c > 126) return;

    static const uint8_t font[][5] = {
        {0x00,0x00,0x00,0x00,0x00}, // 32  space
        {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
        {0x24,0x2E,0x6B,0x6B,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50},
        {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
        {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08}, {0x00,0x50,0x30,0x00,0x00},
        {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
        {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
        {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14}, {0x41,0x22,0x14,0x08,0x00},
        {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
        {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x01,0x01}, {0x3E,0x41,0x41,0x51,0x32},
        {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F},
        {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
        {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03},
        {0x61,0x51,0x49,0x45,0x43}, {0x00,0x00,0x7F,0x41,0x41}, {0x02,0x04,0x08,0x10,0x20},
        {0x41,0x41,0x7F,0x00,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
        {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38},
        {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18},
        {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C}, {0x7F,0x08,0x04,0x04,0x78},
        {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x00,0x7F,0x10,0x28,0x44},
        {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78},
        {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C},
        {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3F,0x44,0x40,0x20},
        {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
        {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44},
        {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00},
        {0x08,0x08,0x2A,0x1C,0x08}, // 126 ~
    };

    const uint8_t* glyph = font[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        int px = cursorX + col;
        if ((unsigned)px >= W) break;
        for (int row = 0; row < 8; row++) {
            if (line & (1 << row)) {
                int py = cursorY + row;
                if ((unsigned)py >= H) continue;
                // Direct pixel set — no drawPixel call overhead
                int idx = px + ((py >> 3) << 7);
                buffer[idx] |= (uint8_t)(1 << (py & 0x07));
            }
        }
    }

    cursorX += 5 + textSize;  // 5px glyph + 1px gap = 6, simplified
    if (cursorX + 5 >= W) {   // wrap if next char won't fit
        cursorX = 0;
        cursorY += (textSize << 3);
    }
}
