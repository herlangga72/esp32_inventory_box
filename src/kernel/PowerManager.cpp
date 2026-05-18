#include "PowerManager.h"
#include "config/Config.h"
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/gpio.h>

// Static variables for deep sleep persistence
RTC_DATA_ATTR int wakeCount = 0;
RTC_DATA_ATTR float savedBaseline = 0.0f;
RTC_DATA_ATTR bool needRecalibration = false;

PowerManager::PowerManager()
    : currentState(POWER_ACTIVE), lastActivityTime(0),
      lightSleepThreshold(10000), deepSleepThreshold(60000),
      pmConfigured(false) {}

void PowerManager::begin() {
    lastActivityTime = millis();
    
    // Check wake reason
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Woke from GPIO (MPU motion)");
            handleWakeFromMotion();
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Woke from timer");
            handleWakeFromTimer();
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            Serial.println("Cold boot");
            handleColdBoot();
            break;
        default:
            Serial.println("Unknown wake reason");
            break;
    }
    
    wakeCount++;
    
    // Configure power management
    configurePowerManagement();
    
    // Setup wake sources for deep sleep
    setupWakeSources();
}

void PowerManager::configurePowerManagement() {
    if (!pmConfigured) {
        esp_pm_config_esp32_t pm_config = {
            .max_cpu_freq = ESP_PM_CPU_FREQ_240M,
            .min_cpu_freq = ESP_PM_CPU_FREQ_80M,
            .light_sleep_enable = true
        };
        
        esp_err_t err = esp_pm_configure(&pm_config);
        if (err == ESP_OK) {
            pmConfigured = true;
            Serial.println("Power management configured");
        } else {
            Serial.printf("PM config failed: %d\n", err);
        }
    }
}

void PowerManager::setupWakeSources() {
    // GPIO wake (MPU interrupt)
    gpio_wakeup_enable(GPIO_NUM_35, GPIO_PIN_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    
    // Timer wake (5 min default)
    esp_sleep_enable_timer_wakeup(deepSleepThreshold * 1000);
}

void PowerManager::onActivity() {
    lastActivityTime = millis();
    
    if (currentState == POWER_LIGHT_SLEEP) {
        exitLightSleep();
        currentState = POWER_ACTIVE;
    }
}

void PowerManager::update() {
    unsigned long idleTime = millis() - lastActivityTime;
    
    switch (currentState) {
        case POWER_ACTIVE:
            if (idleTime > lightSleepThreshold) {
                enterLightSleep();
                currentState = POWER_LIGHT_SLEEP;
            }
            break;
            
        case POWER_LIGHT_SLEEP:
            if (idleTime > deepSleepThreshold) {
                enterDeepSleep();
            } else if (idleTime < lightSleepThreshold) {
                exitLightSleep();
                currentState = POWER_ACTIVE;
            }
            break;
            
        case POWER_DEEP_SLEEP:
            // Waiting for wake
            break;
    }
}

void PowerManager::enterLightSleep() {
    Serial.println("Entering light sleep...");
    currentState = POWER_LIGHT_SLEEP;
}

void PowerManager::exitLightSleep() {
    Serial.println("Exiting light sleep...");
    lastActivityTime = millis();
}

void PowerManager::enterDeepSleep() {
    if (currentState == POWER_DEEP_SLEEP) return;
    
    Serial.println("Entering deep sleep...");
    
    // Save state to RTC memory
    saveStateToRTC();
    
    // Disconnect WiFi to save power
    WiFi.disconnect();
    
    // Enter deep sleep
    currentState = POWER_DEEP_SLEEP;
    esp_deep_sleep_start();
}

void PowerManager::handleWakeFromMotion() {
    currentState = POWER_ACTIVE;
    lastActivityTime = millis();
}

void PowerManager::handleWakeFromTimer() {
    currentState = POWER_ACTIVE;
    lastActivityTime = millis();
    
    // Check if recalibration needed
    if (needRecalibration) {
        Serial.println("Recalibration needed!");
    }
}

void PowerManager::handleColdBoot() {
    currentState = POWER_ACTIVE;
    lastActivityTime = millis();
}

void PowerManager::saveStateToRTC() {
    savedBaseline = currentBaseline;
    wakeCount++;
    // More state saving as needed
}

void PowerManager::restoreStateFromRTC() {
    currentBaseline = savedBaseline;
    Serial.printf("Restored baseline: %.1f\n", currentBaseline);
}

void PowerManager::setBaseline(float baseline) {
    currentBaseline = baseline;
}

int PowerManager::getWakeCount() {
    return wakeCount;
}

PowerState PowerManager::getCurrentState() {
    return currentState;
}

void PowerManager::setThresholds(unsigned long light, unsigned long deep) {
    lightSleepThreshold = light;
    deepSleepThreshold = deep;
}

// Idle hook for tickless operation
void vApplicationIdleHook(void) {
    // This is called when idle task runs
    // ESP32 will enter light sleep automatically with tickless idle
}

// Get baseline (for restoration after deep sleep)
float PowerManager::getBaseline() {
    return currentBaseline;
}