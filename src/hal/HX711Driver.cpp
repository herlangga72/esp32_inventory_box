#include "HX711Driver.h"

HX711Driver::HX711Driver(int dataPin, int clockPin, int drdyPin)
    : pinData(dataPin), pinClock(clockPin), pinDRDY(drdyPin),
      gain(128), offset(0), initialized(false) {}

void HX711Driver::begin() {
    pinMode(pinClock, OUTPUT);
    pinMode(pinData, INPUT_PULLUP);
    if (pinDRDY >= 0) {
        pinMode(pinDRDY, INPUT);
    }
    
    // Power up and set gain
    powerUp();
    setGain(128);
    
    // Read and discard first value
    readRaw();
    
    initialized = true;
}

int32_t HX711Driver::readRaw() {
    // Wait for ready signal with timeout
    unsigned long start = micros();
    while (digitalRead(pinData) == HIGH) {
        if (micros() - start > 100000UL) {  // 100ms timeout
            return INT32_MIN;  // No sensor connected
        }
        vTaskDelay(0);  // yield to other tasks, feed WDT
    }

    delayMicroseconds(10);
    
    // Read 24 bits
    int32_t value = 0;
    for (int i = 23; i >= 0; i--) {
        digitalWrite(pinClock, HIGH);
        delayMicroseconds(1);
        
        if (digitalRead(pinData)) {
            value |= (1 << i);
        }
        
        digitalWrite(pinClock, LOW);
        delayMicroseconds(1);
    }
    
    // Clock out gain setting (128 or 64)
    for (int i = 0; i < gain / 32; i++) {
        digitalWrite(pinClock, HIGH);
        delayMicroseconds(1);
        digitalWrite(pinClock, LOW);
        delayMicroseconds(1);
    }
    
    // Handle two's complement (24-bit signed)
    if (value & 0x800000) {
        value |= 0xFF000000;  // Sign extend
    }
    
    return value - offset;
}

void HX711Driver::setGain(int newGain) {
    gain = (newGain == 64) ? 64 : 128;
}

void HX711Driver::powerDown() {
    digitalWrite(pinClock, LOW);
}

void HX711Driver::powerUp() {
    // Pulse clock to wake up
    digitalWrite(pinClock, HIGH);
    delayMicroseconds(100);
    digitalWrite(pinClock, LOW);
}

bool HX711Driver::isReady() {
    if (pinDRDY >= 0) {
        return digitalRead(pinDRDY) == LOW;
    }
    return digitalRead(pinData) == LOW;
}

void HX711Driver::tare(int samples) {
    int32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += readRaw();
        delay(50);
    }
    if (samples <= 0) return;
    offset = sum / samples;
}