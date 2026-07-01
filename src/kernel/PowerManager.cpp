#include "PowerManager.h"
#include "../utils/LogManager.h"
#include "config/Config.h"
#include <WiFi.h>
#ifndef RELEASE_BUILD
#include <ESPmDNS.h>
#endif
#include <esp_sleep.h>
#include <esp_pm.h>
#include "esp32/pm.h"
#include <driver/gpio.h>

// Static variables for deep sleep persistence
RTC_DATA_ATTR int wakeCount = 0;
RTC_DATA_ATTR float savedBaseline = 0.0f;
RTC_DATA_ATTR bool needRecalibration = false;

PowerManager::PowerManager()
    : currentState(PM_ACTIVE), lastActivityTime(0),
      lightSleepThreshold(30000), deepSleepThreshold(300000),
      pmConfigured(false), opMode(OperationalMode::OP_AP_FULL),
      sleepAllowed(false) {}

void PowerManager::begin(bool setupWake) {
    lastActivityTime = millis();

    // Check wake reason
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
        case ESP_SLEEP_WAKEUP_EXT0:
            LOG_INFO("POWER", "Woke from GPIO (MPU motion)");
            handleWakeFromMotion();
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            LOG_INFO("POWER", "Woke from timer");
            handleWakeFromTimer();
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            LOG_INFO("POWER", "Cold boot");
            handleColdBoot();
            break;
        default:
            LOG_INFO("POWER", "Unknown wake reason");
            break;
    }

    wakeCount++;

    // Configure power management
    configurePowerManagement();

    // Setup wake sources for deep sleep (skip if MPU6050 absent — GPIO35 floats)
    if (setupWake) {
        setupWakeSources();
    }
}

void PowerManager::configurePowerManagement() {
    if (pmConfigured) return;

    esp_pm_config_esp32_t pm_cfg;
    if (opMode == OperationalMode::OP_AP_FULL) {
        pm_cfg.max_freq_mhz = 240;
        pm_cfg.min_freq_mhz = 240;   // locked — no DFS in AP mode
        pm_cfg.light_sleep_enable = false;
    } else {
        pm_cfg.max_freq_mhz = 240;
        pm_cfg.min_freq_mhz = 80;    // DFS allowed in STA mode
        pm_cfg.light_sleep_enable = true;
    }

    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err == ESP_OK) {
        LOG_INFO("POWER", "PM: %s mode, min=%dMHz, light_sleep=%s",
                 (opMode == OperationalMode::OP_AP_FULL ? "AP_FULL" : "STA_IDLE"),
                 (int)pm_cfg.min_freq_mhz,
                 pm_cfg.light_sleep_enable ? "ON" : "OFF");
    } else {
        LOG_WARN("POWER", "PM configure failed: %d", err);
    }
    pmConfigured = true;
}

void PowerManager::setupWakeSources() {
    // GPIO wake (MPU interrupt)
    gpio_wakeup_enable(GPIO_NUM_35, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    
    // Timer wake (5 min default)
    esp_sleep_enable_timer_wakeup(deepSleepThreshold * 1000);
}

void PowerManager::onActivity() {
    lastActivityTime = millis();
    
    if (currentState == PM_LIGHT_SLEEP) {
        exitLightSleep();
        currentState = PM_ACTIVE;
    }
}

void PowerManager::update() {
    if (!sleepAllowed) {
        // AP mode — never sleep, keep idle timer reset
        lastActivityTime = millis();
        return;
    }

    unsigned long idleTime = millis() - lastActivityTime;

    switch (currentState) {
        case PM_ACTIVE:
            if (idleTime > lightSleepThreshold) {
                enterLightSleep();
                currentState = PM_LIGHT_SLEEP;
            }
            break;

        case PM_LIGHT_SLEEP:
            if (deepSleepThreshold > 0 && idleTime > deepSleepThreshold) {
                enterDeepSleep();
            } else if (idleTime < lightSleepThreshold) {
                exitLightSleep();
                currentState = PM_ACTIVE;
            }
            break;

        case PM_DEEP_SLEEP:
            // Waiting for wake
            break;
    }
}

void PowerManager::enterLightSleep() {
    LOG_INFO("POWER", "Entering light sleep...");
    currentState = PM_LIGHT_SLEEP;
}

void PowerManager::exitLightSleep() {
    LOG_INFO("POWER", "Exiting light sleep...");
    lastActivityTime = millis();
}

void PowerManager::enterDeepSleep() {
    if (currentState == PM_DEEP_SLEEP) return;
    
    LOG_INFO("POWER", "Entering deep sleep...");
    
    // Save state to RTC memory
    saveStateToRTC();
    
    // Disconnect WiFi to save power
#ifndef RELEASE_BUILD
    MDNS.end();
#endif
    WiFi.disconnect();
    
    // Enter deep sleep
    currentState = PM_DEEP_SLEEP;
    esp_deep_sleep_start();
}

void PowerManager::handleWakeFromMotion() {
    currentState = PM_ACTIVE;
    lastActivityTime = millis();
}

void PowerManager::handleWakeFromTimer() {
    currentState = PM_ACTIVE;
    lastActivityTime = millis();
    
    // Check if recalibration needed
    if (needRecalibration) {
        LOG_INFO("POWER", "Recalibration needed!");
    }
}

void PowerManager::handleColdBoot() {
    currentState = PM_ACTIVE;
    lastActivityTime = millis();
}

void PowerManager::saveStateToRTC() {
    savedBaseline = currentBaseline;
    wakeCount++;
    // More state saving as needed
}

void PowerManager::restoreStateFromRTC() {
    currentBaseline = savedBaseline;
    LOG_INFO("POWER", "Restored baseline: %.1f", currentBaseline);
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

// Get baseline (for restoration after deep sleep)
float PowerManager::getBaseline() {
    return currentBaseline;
}

void PowerManager::setOperationalMode(OperationalMode mode) {
    if (opMode == mode) return;
    opMode = mode;
    sleepAllowed = (mode == OperationalMode::OP_STA_IDLE);

    // Reconfigure power management for new mode
    pmConfigured = false;
    configurePowerManagement();

    if (mode == OperationalMode::OP_AP_FULL) {
        if (currentState == PM_LIGHT_SLEEP) {
            exitLightSleep();
        }
        currentState = PM_ACTIVE;
        lastActivityTime = millis();
    }
    if (mode == OperationalMode::OP_STA_IDLE) {
        lastActivityTime = millis(); // start sleep countdown fresh
    }
}

bool PowerManager::isSleepAllowed() {
    return sleepAllowed;
}

uint8_t PowerManager::getSpeedFactor() {
    unsigned long idle = millis() - lastActivityTime;
    if (currentState != PM_ACTIVE) return 4;       // sleeping → quarter speed
    if (idle > 30000 && opMode == OperationalMode::OP_STA_IDLE) return 3; // idle 30s+ → third speed
    if (idle > 10000) return 2;                     // idle 10s+ → half speed
    return 1;                                        // active → full speed
}