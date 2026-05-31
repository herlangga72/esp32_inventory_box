#include "InterruptManager.h"
#include "../config/Config.h"

// Mutex globals
SemaphoreHandle_t i2cMutex = NULL;
SemaphoreHandle_t spiffsMutex = NULL;

void i2cLock()   { if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY); }
void i2cUnlock() { if (i2cMutex) xSemaphoreGive(i2cMutex); }
void spiffsLock()   { if (spiffsMutex) xSemaphoreTake(spiffsMutex, portMAX_DELAY); }
void spiffsUnlock() { if (spiffsMutex) xSemaphoreGive(spiffsMutex); }

// Static member definitions
volatile bool InterruptManager::hx711Flag = false;
volatile bool InterruptManager::mpuFlag = false;
volatile bool InterruptManager::buttonFlag = false;
uint32_t InterruptManager::lastButtonPress = 0;

void IRAM_ATTR InterruptManager::hx711ISR(void* arg) {
    hx711Flag = true;
}

void IRAM_ATTR InterruptManager::mpuISR(void* arg) {
    mpuFlag = true;
}

void IRAM_ATTR InterruptManager::buttonISR(void* arg) {
    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    if (now - lastButtonPress > DEBOUNCE_MS) {
        buttonFlag = true;
        lastButtonPress = now;
    }
}

bool InterruptManager::isHX711Ready() {
    bool ready = hx711Flag;
    hx711Flag = false;
    return ready;
}

bool InterruptManager::isMPUTriggered() {
    bool triggered = mpuFlag;
    mpuFlag = false;
    return triggered;
}

bool InterruptManager::isButtonPressed() {
    bool pressed = buttonFlag;
    buttonFlag = false;
    return pressed;
}

void InterruptManager::begin() {
    // HX711 DRDY on GPIO36 (input only)
    gpio_config_t hx711Config = {
        .pin_bit_mask = BIT64(PIN_HX711_DRDY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&hx711Config);
    
    // MPU interrupt on GPIO35 (input only)
    gpio_config_t mpuConfig = {
        .pin_bit_mask = BIT64(PIN_MPU_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    gpio_config(&mpuConfig);
    
    // Button on GPIO33 (input only, with pullup)
    gpio_config_t btnConfig = {
        .pin_bit_mask = BIT64(PIN_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&btnConfig);
    
    // Create mutexes for shared resources
    if (!i2cMutex)    i2cMutex    = xSemaphoreCreateMutex();
    if (!spiffsMutex) spiffsMutex = xSemaphoreCreateMutex();

    // Install GPIO ISR service
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    // Register ISR handlers
    gpio_isr_handler_add((gpio_num_t)PIN_HX711_DRDY, hx711ISR, NULL);
    gpio_isr_handler_add((gpio_num_t)PIN_MPU_INT, mpuISR, NULL);
    gpio_isr_handler_add((gpio_num_t)PIN_BUTTON, buttonISR, NULL);
}