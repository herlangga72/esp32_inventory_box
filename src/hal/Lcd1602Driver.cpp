#include "Lcd1602Driver.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

Lcd1602Driver::Lcd1602Driver()
    : lcd(nullptr), address(LCD1602_ADDR), initialized(false)
{}

Lcd1602Driver::~Lcd1602Driver() {
    if (lcd) {
        delete static_cast<LiquidCrystal_I2C*>(lcd);
        lcd = nullptr;
    }
}

bool Lcd1602Driver::init(uint8_t addr) {
    address = addr;

    if (lcd) {
        delete static_cast<LiquidCrystal_I2C*>(lcd);
        lcd = nullptr;
    }

    auto* impl = new LiquidCrystal_I2C(address, LCD1602_COLS, LCD1602_ROWS);
    if (!impl) return false;

    impl->init();
    impl->backlight();
    impl->clear();

    lcd = impl;
    initialized = true;
    return true;
}

void Lcd1602Driver::clear() {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->clear();
}

void Lcd1602Driver::setCursor(uint8_t col, uint8_t row) {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->setCursor(col, row);
}

void Lcd1602Driver::setBacklight(bool on) {
    if (!initialized) return;
    if (on)
        static_cast<LiquidCrystal_I2C*>(lcd)->backlight();
    else
        static_cast<LiquidCrystal_I2C*>(lcd)->noBacklight();
}

void Lcd1602Driver::print(const char* str) {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->print(str);
}

void Lcd1602Driver::print(const String& str) {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->print(str);
}

void Lcd1602Driver::print(float val, int decimals) {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->print(val, decimals);
}

void Lcd1602Driver::print(int val) {
    if (!initialized) return;
    static_cast<LiquidCrystal_I2C*>(lcd)->print(val);
}
