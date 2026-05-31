#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

class WiFiManager {
public:
    WiFiManager();
    
    void begin();
    void update();
    
    bool isConnected();
    bool isAPMode();
    
    String getIP();
    String getSSID();
    int getRSSI();
    
    void setCredentials(const char* ssid, const char* pass);
    bool hasStoredCredentials();
    
private:
    WebServer* configPortal;
    bool apMode;
    bool staWasConnected;   // track STA drop for reconnection
    bool reconnecting;      // in-progress reconnect attempt
    unsigned long lastStaCheck;
    unsigned long reconnectStart;
    unsigned long connectionStart;
    unsigned long connectionTimeout;

    bool connectSTA();
    void startConfigPortal();
    void stopConfigPortal();
    static void wifiEventCb(WiFiEvent_t event);
    static bool staConnected;
    static bool staTimedOut;
    
    // Captive portal handlers
    void handleRoot();
    void handleSave();
    void handleScan();
};

#endif