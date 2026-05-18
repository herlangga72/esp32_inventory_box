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
    unsigned long connectionStart;
    unsigned long connectionTimeout;
    
    void startConfigPortal();
    void handleConfigPortal();
    void stopConfigPortal();
    
    // Captive portal handlers
    void handleRoot();
    void handleSave();
    void handleScan();
};

#endif