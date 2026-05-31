#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "../hal/SSD1306Driver.h"
#include "../domain/events/EventBus.h"

class DisplayManager {
public:
    enum class Screen {
        STATUS,
        EVENT_LOG,
        SETTINGS,
        CALIBRATION,
        ERROR
    };

    DisplayManager(SSD1306Driver* oled, EventBus* events);

    void init();
    void update();
    bool healthCheck();  // I2C ping → reinit if dead. true = healthy
    void setScreen(Screen screen);
    void showNotification(const char* message);
    void sleep();
    void wake();
    bool isAwake();

private:
    SSD1306Driver* oled;
    EventBus* events;
    Screen currentScreen;
    uint32_t lastRefresh;
    uint32_t lastHealthCheck;
    bool healthy;
    char notificationText[32];
    unsigned long notificationEnd;

    void drawStatusScreen();
    void drawEventLogScreen();
    void drawSettingsScreen();
    void drawCalibrationScreen();
    void drawErrorScreen(const char* message);
    void drawNotification();
    void drawCenteredText(int y, const char* text);

    // Cached values
    float displayWeight;
    float displayBaseline;
    float displayDelta;
    int displayContentCount;
    char displayUser[32];

    // Subscription tokens for cleanup
    SubscriptionToken weightToken;
    SubscriptionToken toolPlacedToken;
    SubscriptionToken toolRemovedToken;
};

#endif // DISPLAY_MANAGER_H
