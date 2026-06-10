#include "GpioDriver.h"
#include <driver/gpio.h>

static bool gpioIsrInstalled = false;

void gpioBegin() {
    if (gpioIsrInstalled) return;
    gpio_install_isr_service(0);
    gpioIsrInstalled = true;
}

void gpioPinMode(int pin, int mode) {
    gpio_config_t c = {};
    c.pin_bit_mask = (1ULL << pin);
    c.intr_type    = GPIO_INTR_DISABLE;
    c.pull_up_en   = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;

    switch (mode) {
        case GPIO_OUTPUT:
            c.mode = GPIO_MODE_OUTPUT;
            break;
        case GPIO_INPUT_PULLUP:
            c.pull_up_en = GPIO_PULLUP_ENABLE;
            // fallthrough
        default:
            c.mode = GPIO_MODE_INPUT;
            break;
    }
    gpio_config(&c);
}

void gpioDigitalWrite(int pin, int value) {
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);
}

int gpioDigitalRead(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}
