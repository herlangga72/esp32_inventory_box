#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>

class WebServer;  // forward declare — full include only in .cpp (AP mode)

// WiFi mode determines which overlay is active.
// AP and STA never run simultaneously — their state shares memory.
enum class WiFiOpMode : uint8_t { OFF, STA, AP };

// ---- STA-mode state (only valid when mode == STA) ----
struct WiFiSTAState {
    wifi_config_t     staConfig;
    wifi_init_config_t initCfg;
    esp_netif_t*      netif;
    bool              connected;
    bool              wasConnected;
    bool              reconnecting;
    unsigned long     lastCheck;
    unsigned long     reconnectStart;
    unsigned long     connTimeout;
    char              ip[16];
};

// ---- AP-mode state (only valid when mode == AP) ----
struct WiFiAPState {
    wifi_config_t apConfig;
    WebServer*    portal;
};

class WiFiManager {
public:
    WiFiManager();

    void begin();
    void update();

    bool isConnected();
    bool isAPMode();
    WiFiOpMode getMode() const { return mode; }

    const char* getIP();
    const char* getSSID();
    int getRSSI();

    void setCredentials(const char* ssid, const char* pass);
    bool hasStoredCredentials();

    // Memory reporting
    size_t getModeStaticMem() const;
    size_t getModePoolUsed() const;
    size_t getModePoolSize() const;

    // Static OSI allocator callbacks (STA mode only, from registry wifiPool)
    static void* wifiMalloc(size_t size);
    static void  wifiFree(void* ptr);

private:
    // ---- Shared (both modes) ----
    WiFiOpMode   mode;
    char         ssid[33];
    unsigned long connStart;

    // ---- Mode overlay: STA state OR AP state, never both ----
    union {
        WiFiSTAState sta;
        WiFiAPState  ap;
    };

    bool connectSTA();
    void freeModePool();
    void startConfigPortal();
    void stopConfigPortal();

    static void eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
    static WiFiManager* s_instance;

    void handleRoot();
    void handleSave();
    void handleScan();
};

#endif
