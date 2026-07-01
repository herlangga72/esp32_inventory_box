#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include "../config/Config.h"

enum PowerState {
    PM_ACTIVE,
    PM_LIGHT_SLEEP,
    PM_DEEP_SLEEP
};

class PowerManager {
public:
    PowerManager();
    
    void begin(bool setupWake = true);
    void update();
    void onActivity();
    
    void enterLightSleep();
    void exitLightSleep();
    void enterDeepSleep();
    
    void handleWakeFromMotion();
    void handleWakeFromTimer();
    void handleColdBoot();
    
    PowerState getCurrentState();
    int getWakeCount();
    float getBaseline();
    void setBaseline(float baseline);
    void setThresholds(unsigned long lightMs, unsigned long deepMs);
    void setOperationalMode(OperationalMode mode);
    bool isSleepAllowed();
    uint8_t getSpeedFactor();  // 1=full, 2=half, 4=quarter

private:
    PowerState currentState;
    unsigned long lastActivityTime;
    unsigned long lightSleepThreshold;
    unsigned long deepSleepThreshold;
    float currentBaseline;
    bool pmConfigured;
    OperationalMode opMode;
    bool sleepAllowed;

    void configurePowerManagement();
    void setupWakeSources();
    void saveStateToRTC();
    void restoreStateFromRTC();
};

#endif // POWER_MANAGER_H