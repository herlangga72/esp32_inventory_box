#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>

// ESP-IDF native GPIO — zero Arduino dependency.
// Maps to Arduino pinMode/digitalWrite/digitalRead semantics
// but uses driver/gpio.h directly. Compile-tested on ESP32.

// ---- Init ----
void gpioBegin();  // one-time GPIO ISR service install (call once in boot)

// ---- Pin config ----
void gpioPinMode(int pin, int mode);
#define GPIO_OUTPUT       1
#define GPIO_INPUT        2
#define GPIO_INPUT_PULLUP 3

// ---- Digital I/O ----
void gpioDigitalWrite(int pin, int value);
int  gpioDigitalRead(int pin);

// ---- Analog (ESP32 DAC/ADC) ----
// Stubs for now — ADC needs careful calibration. Falls back to Arduino.
#ifndef GPIO_NO_ARDUINO_FALLBACK
#include <Arduino.h>
inline void gpioAnalogWrite(int pin, int value) { analogWrite(pin, value); }
inline int  gpioAnalogRead(int pin)             { return analogRead(pin); }
#endif

#endif // GPIO_DRIVER_H
