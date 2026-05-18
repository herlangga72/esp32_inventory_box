#include "DisplayManager.h"
#include "../domain/events/Events.h"
#include <string.h>

DisplayManager::DisplayManager(SSD1306Driver* oled, EventBus* events)
    : oled(oled), events(events), currentScreen(Screen::STATUS),
      lastRefresh(0), notificationEnd(0) {
    displayWeight = 0;
    displayBaseline = 0;
    displayDelta = 0;
    displayContentCount = 0;
    displayUser[0] = '\0';
}

void DisplayManager::init() {
    // Subscribe to events
    events->subscribe(DomainEvent::WEIGHT_UPDATED, [this](const EventPayload& e) {
        displayWeight = e.data.weight.weight;
        displayDelta = e.data.weight.delta;
        displayBaseline = e.data.weight.baseline;
    });
    
    events->subscribe(DomainEvent::TOOL_PLACED, [this](const EventPayload& e) {
        displayContentCount++;
        showNotification("Tool placed");
    });
    
    events->subscribe(DomainEvent::TOOL_REMOVED, [this](const EventPayload& e) {
        displayContentCount--;
        showNotification("Tool removed");
    });
}

void DisplayManager::update() {
    if (millis() - lastRefresh < 1000) return;  // 1Hz refresh
    
    switch (currentScreen) {
        case Screen::STATUS:
            drawStatusScreen();
            break;
        case Screen::EVENT_LOG:
            drawEventLogScreen();
            break;
        case Screen::SETTINGS:
            drawSettingsScreen();
            break;
        case Screen::CALIBRATION:
            drawCalibrationScreen();
            break;
        case Screen::ERROR:
            drawErrorScreen("System Error");
            break;
    }
    
    // Draw notification if active
    if (millis() < notificationEnd) {
        drawNotification();
    }
    
    oled->display();
    lastRefresh = millis();
}

void DisplayManager::setScreen(Screen screen) {
    currentScreen = screen;
}

void DisplayManager::showNotification(const char* message) {
    strncpy(notificationText, message, sizeof(notificationText) - 1);
    notificationEnd = millis() + 3000;
}

void DisplayManager::sleep() {
    oled->sleep();
}

void DisplayManager::wake() {
    oled->wake();
}

bool DisplayManager::isAwake() {
    return oled->isAwake();
}

void DisplayManager::drawStatusScreen() {
    oled->clear();
    
    // Weight - large font
    char weightStr[16];
    snprintf(weightStr, sizeof(weightStr), "%.1f g", displayWeight);
    drawCenteredText(0, weightStr);
    
    // Baseline / Delta
    oled->setTextSize(1);
    char infoStr[32];
    snprintf(infoStr, sizeof(infoStr), "B: %.1f  D: %+.1f", displayBaseline, displayDelta);
    oled->setCursor(16, 20);
    oled->print(infoStr);
    
    // Contents count
    oled->setCursor(0, 32);
    oled->print("Contents: ");
    oled->print(displayContentCount);
    
    // User
    oled->setCursor(0, 44);
    oled->print("User: ");
    oled->print(strlen(displayUser) > 0 ? displayUser : "None");
    
    // WiFi RSSI
    oled->setCursor(0, 56);
    oled->print("WiFi: ");
    oled->print(WiFi.RSSI());
    oled->print(" dBm");
}

void DisplayManager::drawEventLogScreen() {
    oled->clear();
    
    oled->setTextSize(1);
    oled->setCursor(0, 0);
    oled->print("Event Log");
    
    // Show last 6 events (simplified)
    oled->setCursor(0, 16);
    oled->print("Recent events...");
    
    oled->setCursor(0, 56);
    oled->print("[Button] Back");
}

void DisplayManager::drawSettingsScreen() {
    oled->clear();
    
    oled->setTextSize(1);
    oled->setCursor(0, 0);
    oled->print("Settings");
    
    oled->setCursor(0, 16);
    oled->print("1. Calibrate");
    oled->setCursor(0, 28);
    oled->print("2. WiFi Config");
    oled->setCursor(0, 40);
    oled->print("3. Sleep Mode");
    
    oled->setCursor(0, 56);
    oled->print("[Button] Back");
}

void DisplayManager::drawCalibrationScreen() {
    oled->clear();
    
    oled->setTextSize(1);
    drawCenteredText(20, "Calibrating...");
    drawCenteredText(36, "Place empty box");
    
    // Progress bar
    oled->drawRect(20, 50, 88, 8, 1);
    
    oled->setCursor(0, 56);
    oled->print("[Button] Cancel");
}

void DisplayManager::drawErrorScreen(const char* message) {
    oled->clear();
    
    oled->setTextSize(1);
    drawCenteredText(20, "ERROR");
    drawCenteredText(32, message);
    
    oled->setCursor(0, 56);
    oled->print("[Button] Retry");
}

void DisplayManager::drawNotification() {
    // Small banner at bottom
    oled->fillRect(0, 58, 128, 6, 1);
    oled->setTextColor(0);
    oled->setCursor(4, 59);
    oled->print(notificationText);
    oled->setTextColor(1);
}

void DisplayManager::drawCenteredText(int y, const char* text) {
    int len = strlen(text);
    int x = (128 - len * 6) / 2;  // Approximate centering
    if (x < 0) x = 0;
    oled->setCursor(x, y);
    oled->print(text);
}