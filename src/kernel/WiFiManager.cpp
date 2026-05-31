#include "WiFiManager.h"
#include <SPIFFS.h>
#include <esp_wifi.h>
#include "../data/StorageManager.h"
#include "../utils/LogManager.h"
#include "../utils/JsonBuilder.h"
#include "../utils/JsonParser.h"
#include "SystemStatus.h"
#include "PowerManager.h"
#include "../hal/InterruptManager.h"

extern StorageManager storage;
extern PowerManager powerManager;

const char* CONFIG_AP_SSID = "Inventory-Box-Setup";
const char* CONFIG_AP_PASS = "12345678";
const IPAddress CONFIG_AP_IP(192, 168, 4, 1);

bool WiFiManager::staConnected = false;
bool WiFiManager::staTimedOut = false;

WiFiManager::WiFiManager()
    : configPortal(nullptr), apMode(false),
      staWasConnected(false), reconnecting(false), lastStaCheck(0),
      reconnectStart(0), connectionStart(0), connectionTimeout(15000) {}

void WiFiManager::wifiEventCb(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            staConnected = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            staTimedOut = true;
            break;
        default:
            break;
    }
}

bool WiFiManager::connectSTA() {
    String ssid = storage.getString("wifi_ssid", "");
    String pass = storage.getString("wifi_pass", "");
    if (ssid.length() == 0) return false;

    LOG_INFO("WIFI", "Connecting to %s...", ssid.c_str());

    staConnected = false;
    staTimedOut = false;
    WiFi.onEvent(wifiEventCb);

    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(200);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (millis() - start < connectionTimeout) {
        if (staConnected) {
            WiFi.removeEvent(wifiEventCb);
            WiFi.setSleep(false);  // keep radio on — prevent modem sleep disconnects
            LOG_INFO("WIFI", "STA connected: %s  IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            return true;
        }
        if (staTimedOut) {
            WiFi.removeEvent(wifiEventCb);
            LOG_WARN("WIFI", "STA disconnected (auth fail or out of range)");
            return false;
        }
        delay(100);
        yield();  // feed WDT during long STA connect
    }

    WiFi.removeEvent(wifiEventCb);
    LOG_WARN("WIFI", "STA connection timed out after %lums", connectionTimeout);
    return false;
}

void WiFiManager::begin() {
    if (hasStoredCredentials()) {
        LOG_INFO("WIFI", "Credentials found, attempting STA...");
        if (connectSTA()) {
            apMode = false;
            staWasConnected = true;
            lastStaCheck = millis();
            SystemStatus::getInstance().setOperationalMode(OperationalMode::OP_STA_IDLE);
            powerManager.setOperationalMode(OperationalMode::OP_STA_IDLE);
            return;
        }
        LOG_WARN("WIFI", "STA failed, falling back to AP...");
    }
    LOG_INFO("WIFI", "Starting AP mode (STA disabled)...");
    startConfigPortal();
    SystemStatus::getInstance().setOperationalMode(OperationalMode::OP_AP_FULL);
    powerManager.setOperationalMode(OperationalMode::OP_AP_FULL);
}

void WiFiManager::update() {
    if (apMode && configPortal) {
        configPortal->handleClient();
        return;
    }

    // STA mode: non-blocking reconnect state machine
    if (!apMode && staWasConnected) {
        // Phase 1: periodic health check
        if (!reconnecting && (millis() - lastStaCheck > 5000)) {
            lastStaCheck = millis();
            if (WiFi.status() != WL_CONNECTED) {
                LOG_WARN("WIFI", "STA disconnected — starting reconnect...");
                WiFi.reconnect();
                reconnecting = true;
                reconnectStart = millis();
            }
        }

        // Phase 2: poll reconnect result (non-blocking)
        if (reconnecting) {
            if (WiFi.status() == WL_CONNECTED) {
                reconnecting = false;
                LOG_INFO("WIFI", "Reconnected — IP: %s", WiFi.localIP().toString().c_str());
            } else if (millis() - reconnectStart > 10000) {
                LOG_ERROR("WIFI", "Reconnect failed — falling back to AP");
                reconnecting = false;
                WiFi.mode(WIFI_OFF);
                delay(200);
                startConfigPortal();
                SystemStatus::getInstance().setOperationalMode(OperationalMode::OP_AP_FULL);
                powerManager.setOperationalMode(OperationalMode::OP_AP_FULL);
            }
        }
    }
}

bool WiFiManager::isConnected() {
    return apMode || (WiFi.status() == WL_CONNECTED);
}

bool WiFiManager::isAPMode() {
    return apMode;
}

String WiFiManager::getIP() {
    if (apMode) {
        return CONFIG_AP_IP.toString();
    }
    return WiFi.localIP().toString();
}

String WiFiManager::getSSID() {
    if (apMode) {
        return String(CONFIG_AP_SSID);
    }
    return WiFi.SSID();
}

int WiFiManager::getRSSI() {
    if (apMode) return 0;
    return WiFi.RSSI();
}

void WiFiManager::setCredentials(const char* ssid, const char* pass) {
    storage.putString("wifi_ssid", ssid);
    storage.putString("wifi_pass", pass);
}

bool WiFiManager::hasStoredCredentials() {
    String ssid = storage.getString("wifi_ssid", "");
    return ssid.length() > 0;
}

void WiFiManager::startConfigPortal() {
    apMode = true;

    // Full WiFi reset — prevent mode-switch failures
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(200);

    WiFi.setSleep(false);  // keep radio on — critical for AP visibility

    // Force 20MHz b/g/n on channel 6 (avoid congested channel 1)
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

    // Start AP on channel 6
    WiFi.softAPConfig(CONFIG_AP_IP, CONFIG_AP_IP, IPAddress(255, 255, 255, 0));

    bool apOk = WiFi.softAP(CONFIG_AP_SSID, NULL, 6);
    if (!apOk) {
        LOG_ERROR("WIFI", "softAP FAILED — retry channel 1...");
        WiFi.mode(WIFI_OFF);
        delay(200);
        WiFi.mode(WIFI_AP);
        delay(200);
        apOk = WiFi.softAP(CONFIG_AP_SSID);
    }

    if (apOk) {
        WiFi.setTxPower(WIFI_POWER_19_5dBm);  // max range for phone visibility
        LOG_INFO("WIFI", "AP active: %s", CONFIG_AP_SSID);
        LOG_INFO("WIFI", "IP: %s  MAC: %s", CONFIG_AP_IP.toString().c_str(), WiFi.softAPmacAddress().c_str());
        LOG_INFO("WIFI", "Channel: %d  TX: 19.5dBm  Sleep: OFF", WiFi.channel());
    } else {
        LOG_ERROR("WIFI", "softAP FAILED after retry — AP not broadcasting");
    }
    
    // Setup web server
    configPortal = new WebServer(CONFIG_AP_IP, 80);
    if (!configPortal) {
        LOG_ERROR("WIFI", "OOM: WebServer allocation failed — AP not available");
        apMode = false;
        return;
    }

    // Setup web routes
    configPortal->on("/", [this]() { handleRoot(); });
    configPortal->on("/save", [this]() { handleSave(); });
    configPortal->on("/scan", [this]() { handleScan(); });
    configPortal->onNotFound([this]() { handleRoot(); });
    
    configPortal->begin();
}

void WiFiManager::stopConfigPortal() {
    if (configPortal) {
        configPortal->stop();
        delete configPortal;
        configPortal = nullptr;
    }
    WiFi.softAPdisconnect(true);
    apMode = false;
}

void WiFiManager::handleRoot() {
    spiffsLock();
    if (SPIFFS.exists("/wifi-setup.html")) {
        File file = SPIFFS.open("/wifi-setup.html", "r");
        configPortal->streamFile(file, "text/html");
        file.close();
        spiffsUnlock();
        return;
    }
    spiffsUnlock();
    // Fallback if SPIFFS not uploaded
    configPortal->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>WiFi Setup</title></head><body style='font-family:Arial;text-align:center;padding:50px;background:#e8eef4'>"
        "<h1>WiFi Setup</h1><p>SPIFFS files not uploaded. Please upload SPIFFS first.</p>"
        "</body></html>");
}

void WiFiManager::handleSave() {
    if (configPortal->method() != HTTP_POST) return;

    struct { char ssid[33]; char pass[65]; } req;
    memset(&req, 0, sizeof(req));

    JField fields[] = {
        {"ssid",     JField::T_STR, req.ssid, sizeof(req.ssid)},
        {"password", JField::T_STR, req.pass, sizeof(req.pass)},
    };
    String err;
    if (!jsonParse(configPortal->arg("plain").c_str(), fields, 2, err)) {
        configPortal->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    if (req.ssid[0] == '\0') {
        configPortal->send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
        return;
    }

    setCredentials(req.ssid, req.pass);
    LOG_INFO("WIFI", "Credentials saved for: %s", req.ssid);
    configPortal->send(200, "application/json", "{\"success\":true}");

    delay(1000);
    ESP.restart();
}

void WiFiManager::handleScan() {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        configPortal->send(200, "application/json", "{\"networks\":[]}");
        return;
    }

    if (n == WIFI_SCAN_RUNNING) {
        configPortal->send(200, "application/json", "{\"networks\":[]}");
        return;
    }

    JsonBuilder jb;
    jb.startObj();
    jb.startArr("networks");
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        jb.startArrObj();
        jb.addStr("ssid", ssid.c_str());
        jb.addInt("rssi", WiFi.RSSI(i));
        jb.endObj();
    }
    jb.endArr();
    jb.endObj();

    WiFi.scanDelete();
    configPortal->send(200, "application/json", jb.str());
}