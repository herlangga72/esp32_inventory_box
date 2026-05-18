#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_pm.h>

enum class PowerState {
    POWER_ACTIVE,
    POWER_LIGHT_SLEEP,
    POWER_DEEP_SLEEP
};

class PowerManager {
public:
    PowerManager();
    
    void begin();
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

private:
    PowerState currentState;
    unsigned long lastActivityTime;
    unsigned long lightSleepThreshold;
    unsigned long deepSleepThreshold;
    float currentBaseline;
    bool pmConfigured;
    
    void configurePowerManagement();
    void setupWakeSources();
    void saveStateToRTC();
    void restoreStateFromRTC();
};

#endif // POWER_MANAGER_H