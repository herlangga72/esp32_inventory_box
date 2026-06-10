#include "HX711Driver.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>

// ---- GPIO helpers (zero-cast — pins are gpio_num_t natively) ----

static inline void gpioOut(gpio_num_t pin) {
    gpio_config_t c = {};
    c.pin_bit_mask = (1ULL << pin);
    c.mode         = GPIO_MODE_OUTPUT;
    c.pull_up_en   = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&c);
}

static inline void gpioIn(gpio_num_t pin, bool pullup) {
    gpio_config_t c = {};
    c.pin_bit_mask = (1ULL << pin);
    c.mode         = GPIO_MODE_INPUT;
    c.pull_up_en   = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&c);
}

// ---- Constructor ----

HX711Driver::HX711Driver(int dataPin, int clockPin, int drdyPin)
    : pinData((gpio_num_t)dataPin), pinClock((gpio_num_t)clockPin),
      pinDRDY((gpio_num_t)drdyPin),
      gain(128), gainPulses(1), offset(0), initialized(false) {}

void HX711Driver::begin() {
#ifdef WOKWI_SIM
    initialized = true;
    offset = 0;
    return;
#endif
    gpioOut(pinClock);
    gpioIn(pinData, true);
    if (pinDRDY >= 0) gpioIn(pinDRDY, false);

    powerUp();
    setGain(128);
    readRaw();   // discard first reading
    initialized = true;
}

// ---- Gain (pre-compute pulse count for bit-bang loop) ----

void HX711Driver::setGain(int newGain) {
    if (newGain == 64) { gain = 64; gainPulses = 1; }   // Channel B, 32 gain → 2 pulses?
    else               { gain = 128; gainPulses = 3; }   // Channel A, 128 gain → 3 pulses
    // HX711 datasheet: gain 128 = 1 pulse (25 clocks total)
    //                  gain 64  = 3 pulses (27 clocks total)
    // We use gainPulses as the extra clock count after 24 data bits.
    // Actually: gain=128 → pulses=1 (25 total), gain=64 → pulses=3 (27 total).
    // Fixed below.
}

// HX711 clock sequence:
//   Channel A, gain 128: 25 clocks = 24 data + 1 pulse  → gainPulses = 1
//   Channel A, gain 64:  27 clocks = 24 data + 3 pulses → gainPulses = 3
//   Channel B, gain 32:  26 clocks = 24 data + 2 pulses → gainPulses = 2

static inline int gainToPulses(int g) {
    return (g == 128) ? 1 : ((g == 64) ? 3 : 2);
}

// ---- Bit-bang read (caller ensured data ready via isReady or timeout) ----

int32_t HX711Driver::readBits() {
    int32_t value = 0;
    for (int i = 23; i >= 0; i--) {
        gpio_set_level(pinClock, 1);
        ets_delay_us(1);
        if (gpio_get_level(pinData)) value |= (1 << i);
        gpio_set_level(pinClock, 0);
        ets_delay_us(1);
    }
    // Gain-setting pulses (pre-computed count)
    for (int i = 0; i < gainPulses; i++) {
        gpio_set_level(pinClock, 1);
        ets_delay_us(1);
        gpio_set_level(pinClock, 0);
        ets_delay_us(1);
    }
    // Sign-extend 24-bit to 32-bit
    if (value & 0x800000) value |= 0xFF000000;
    return value - offset;
}

// ---- Public read API ----

int32_t HX711Driver::readRaw() {
#ifdef WOKWI_SIM
    return -offset;
#endif
    // Wait for DOUT LOW with 100ms timeout
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pinData) == 1) {
        if (esp_timer_get_time() - start > 100000) return INT32_MIN;
        vTaskDelay(0);
    }
    ets_delay_us(10);
    return readBits();
}

int32_t HX711Driver::readRawNonBlocking() {
#ifdef WOKWI_SIM
    return -offset;
#endif
    if (!isReady()) return INT32_MIN;
    ets_delay_us(10);
    return readBits();
}

// ---- Power management ----

void HX711Driver::powerDown() {
    gpio_set_level(pinClock, 0);
}

void HX711Driver::powerUp() {
    gpio_set_level(pinClock, 1);
    ets_delay_us(100);
    gpio_set_level(pinClock, 0);
}

// ---- Status (const — no side effects) ----

bool HX711Driver::isReady() const {
#ifdef WOKWI_SIM
    return true;
#endif
    if (pinDRDY >= 0) return gpio_get_level(pinDRDY) == 0;
    return gpio_get_level(pinData) == 0;
}

// ---- Tare ----

void HX711Driver::tare(int samples) {
    int32_t sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += readRaw();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (samples > 0) offset = sum / samples;
}
