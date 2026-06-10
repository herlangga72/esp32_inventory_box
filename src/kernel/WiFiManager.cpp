#include "WiFiManager.h"
#include <WebServer.h>    // AP mode only — heavy include kept out of header
#include <SPIFFS.h>
#include "../kernel/ServiceRegistry.h"
#include "../data/StorageManager.h"
#include "../utils/LogManager.h"
#include "../utils/JsonBuilder.h"
#include "../utils/JsonParser.h"
#include "SystemStatus.h"
#include "PowerManager.h"
#include "../hal/InterruptManager.h"
#include <cstring>

extern StorageManager storage;
extern PowerManager powerManager;

static const char* AP_SSID = "Inventory-Box-Setup";

WiFiManager* WiFiManager::s_instance = nullptr;

// ======================================================================
// Static OSI allocator — STA mode only, from registry wifiPool
// ======================================================================
void* WiFiManager::wifiMalloc(size_t size) {
    void* ptr = &g_registry.wifiPool[g_registry.wifiPoolUsed];
    size_t aligned = (size + 3) & ~3;
    if (g_registry.wifiPoolUsed + aligned > SR_WIFI_POOL_SIZE) {
        LOG_ERROR("WIFI", "OSI pool exhausted! need=%d", (int)aligned);
        return nullptr;
    }
    g_registry.wifiPoolUsed += aligned;
    return ptr;
}

void WiFiManager::wifiFree(void* ptr) { (void)ptr; }

// ======================================================================
// Event handler (STA mode only)
// ======================================================================
void WiFiManager::eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    WiFiManager* self = static_cast<WiFiManager*>(arg);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        self->sta.connected = true;
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)data;
        snprintf(self->sta.ip, sizeof(self->sta.ip), IPSTR, IP2STR(&evt->ip_info.ip));
        LOG_INFO("WIFI", "STA got IP: %s", self->sta.ip);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->sta.connected = false;
        LOG_INFO("WIFI", "STA disconnected");
    }
}

// ======================================================================
WiFiManager::WiFiManager()
    : mode(WiFiOpMode::OFF), connStart(0) {
    ssid[0] = '\0';
    s_instance = this;
    // Zero both overlays
    memset(&sta, 0, sizeof(sta));
    memset(&ap, 0, sizeof(ap));
}

// ======================================================================
// Free any mode-specific pool usage before switching modes
// ======================================================================
void WiFiManager::freeModePool() {
    if (mode == WiFiOpMode::AP && ap.portal) {
        ap.portal->stop();
        ap.portal->~WebServer();
        ap.portal = nullptr;
        WiFi.softAPdisconnect(true);
    }
    if (mode == WiFiOpMode::STA) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        if (sta.netif) { esp_netif_destroy(sta.netif); sta.netif = nullptr; }
    }
    g_registry.wifiPoolUsed = 0;
    mode = WiFiOpMode::OFF;
}

// ======================================================================
// STA connect — uses registry wifiPool for OSI allocator
// ======================================================================
bool WiFiManager::connectSTA() {
    char ssidBuf[33], passBuf[65];
    if (storage.getChars("wifi_ssid", ssidBuf, sizeof(ssidBuf)) == 0) return false;
    storage.getChars("wifi_pass", passBuf, sizeof(passBuf));

    LOG_INFO("WIFI", "STA: connecting to %s...", ssidBuf);

    g_registry.wifiPoolUsed = 0;
    mode = WiFiOpMode::STA;

    esp_netif_init();
    esp_event_loop_create_default();
    sta.netif = esp_netif_create_default_wifi_sta();

    wifi_osi_funcs_t osi = {};
    osi._malloc = wifiMalloc;
    osi._free   = wifiFree;
    sta.initCfg = WIFI_INIT_CONFIG_DEFAULT();
    sta.initCfg.osi_funcs = &osi;

    ESP_ERROR_CHECK(esp_wifi_init(&sta.initCfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, this));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    strncpy((char*)sta.staConfig.sta.ssid, ssidBuf, sizeof(sta.staConfig.sta.ssid) - 1);
    strncpy((char*)sta.staConfig.sta.password, passBuf, sizeof(sta.staConfig.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta.staConfig));

    sta.connected = false;
    sta.connTimeout = 15000;
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    unsigned long start = millis();
    while (millis() - start < sta.connTimeout) {
        if (sta.connected) {
            strncpy(ssid, ssidBuf, sizeof(ssid) - 1);
            LOG_INFO("WIFI", "STA connected: %s  IP: %s", ssid, sta.ip);
            return true;
        }
        delay(100); yield();
    }
    LOG_WARN("WIFI", "STA connection timed out");
    return false;
}

// ======================================================================
void WiFiManager::begin() {
    if (hasStoredCredentials()) {
        LOG_INFO("WIFI", "Credentials found, attempting STA...");
        if (connectSTA()) {
            sta.wasConnected = true;
            sta.lastCheck = millis();
            systemStatus_setOperationalMode(OperationalMode::OP_STA_IDLE);
            powerManager.setOperationalMode(OperationalMode::OP_STA_IDLE);
            return;
        }
        LOG_WARN("WIFI", "STA failed, falling back to AP...");
        freeModePool();
    }
    LOG_INFO("WIFI", "Starting AP mode...");
    startConfigPortal();
    systemStatus_setOperationalMode(OperationalMode::OP_AP_FULL);
    powerManager.setOperationalMode(OperationalMode::OP_AP_FULL);
}

// ======================================================================
void WiFiManager::update() {
    if (mode == WiFiOpMode::AP) {
        if (ap.portal) ap.portal->handleClient();
        return;
    }

    if (mode == WiFiOpMode::STA && sta.wasConnected) {
        if (!sta.reconnecting && (millis() - sta.lastCheck > 5000)) {
            sta.lastCheck = millis();
            wifi_ap_record_t apInfo;
            if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK) {
                LOG_WARN("WIFI", "STA disconnected — reconnecting...");
                esp_wifi_connect();
                sta.reconnecting = true;
                sta.reconnectStart = millis();
            }
        }
        if (sta.reconnecting) {
            if (sta.connected) {
                sta.reconnecting = false;
                LOG_INFO("WIFI", "Reconnected — IP: %s", sta.ip);
            } else if (millis() - sta.reconnectStart > 10000) {
                LOG_ERROR("WIFI", "Reconnect failed — falling back to AP");
                sta.reconnecting = false;
                freeModePool();
                startConfigPortal();
                systemStatus_setOperationalMode(OperationalMode::OP_AP_FULL);
                powerManager.setOperationalMode(OperationalMode::OP_AP_FULL);
            }
        }
    }
}

bool WiFiManager::isConnected() { return mode == WiFiOpMode::AP || sta.connected; }
bool WiFiManager::isAPMode()   { return mode == WiFiOpMode::AP; }
const char* WiFiManager::getIP()    { return (mode == WiFiOpMode::AP) ? "192.168.4.1" : sta.ip; }
const char* WiFiManager::getSSID()  { return (mode == WiFiOpMode::AP) ? AP_SSID : ssid; }

int WiFiManager::getRSSI() {
    if (mode != WiFiOpMode::STA) return 0;
    wifi_ap_record_t apInfo;
    return (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) ? apInfo.rssi : 0;
}

void WiFiManager::setCredentials(const char* s, const char* p) {
    storage.putString("wifi_ssid", s);
    storage.putString("wifi_pass", p);
}

bool WiFiManager::hasStoredCredentials() {
    char buf[33];
    return storage.getChars("wifi_ssid", buf, sizeof(buf)) > 0;
}

// ======================================================================
// Memory reporting
// ======================================================================
size_t WiFiManager::getModeStaticMem() const {
    if (mode == WiFiOpMode::AP) return sizeof(WiFiAPState);
    if (mode == WiFiOpMode::STA) return sizeof(WiFiSTAState);
    return 0;
}

size_t WiFiManager::getModePoolUsed() const { return g_registry.wifiPoolUsed; }

size_t WiFiManager::getModePoolSize() const {
    // STA: full wifiPool for OSI. AP: sizeof(WebServer) + margin
    if (mode == WiFiOpMode::STA) return SR_WIFI_POOL_SIZE;
    if (mode == WiFiOpMode::AP)  return sizeof(WebServer) + 512;
    return 0;
}

// ======================================================================
// AP mode — placement-new WebServer into wifiPool (reuses STA OSI buffer)
// ======================================================================
void WiFiManager::startConfigPortal() {
    g_registry.wifiPoolUsed = 0;
    mode = WiFiOpMode::AP;

    WiFi.mode(WIFI_OFF);
    delay(20);
    WiFi.mode(WIFI_AP);
    delay(20);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    WiFi.softAP(AP_SSID, NULL);
    LOG_INFO("WIFI", "AP active: %s  IP: 192.168.4.1", AP_SSID);

    // Placement-new WebServer into wifiPool
    ap.portal = new (g_registry.wifiPool) WebServer(IPAddress(192,168,4,1), 80);
    ap.portal->on("/", [this]() { handleRoot(); });
    ap.portal->on("/save", [this]() { handleSave(); });
    ap.portal->on("/scan", [this]() { handleScan(); });
    ap.portal->onNotFound([this]() { handleRoot(); });
    ap.portal->begin();
}

void WiFiManager::stopConfigPortal() {
    if (ap.portal) {
        ap.portal->stop();
        ap.portal->~WebServer();
        ap.portal = nullptr;
    }
    WiFi.softAPdisconnect(true);
    mode = WiFiOpMode::OFF;
}

void WiFiManager::handleRoot() {
    spiffsLock();
    if (SPIFFS.exists("/wifi-setup.html")) {
        File file = SPIFFS.open("/wifi-setup.html", "r");
        ap.portal->streamFile(file, "text/html");
        file.close();
        spiffsUnlock();
        return;
    }
    spiffsUnlock();
    ap.portal->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>WiFi Setup</title></head><body style='font-family:Arial;text-align:center;padding:50px;background:#e8eef4'>"
        "<h1>WiFi Setup</h1><p>SPIFFS files not uploaded.</p></body></html>");
}

void WiFiManager::handleSave() {
    if (ap.portal->method() != HTTP_POST) return;
    struct { char ssid[33]; char pass[65]; } req; memset(&req, 0, sizeof(req));
    JField fields[] = {
        {"ssid", JField::T_STR, req.ssid, sizeof(req.ssid)},
        {"password", JField::T_STR, req.pass, sizeof(req.pass)},
    };
    String err;
    if (!jsonParse(ap.portal->arg("plain").c_str(), fields, 2, err)) {
        ap.portal->send(400, "application/json", "{\"success\":false}"); return;
    }
    if (req.ssid[0] == '\0') {
        ap.portal->send(400, "application/json", "{\"success\":false}"); return;
    }
    setCredentials(req.ssid, req.pass);
    LOG_INFO("WIFI", "Credentials saved for: %s", req.ssid);
    ap.portal->send(200, "application/json", "{\"success\":true}");
    delay(1000); ESP.restart();
}

void WiFiManager::handleScan() {
    int n = WiFi.scanComplete();
    if (n <= 0) { WiFi.scanNetworks(true); ap.portal->send(200, "application/json", "{\"networks\":[]}"); return; }
    JsonBuilder jb; jb.startObj(); jb.startArr("networks");
    for (int i = 0; i < n; i++) {
        String netSsid = WiFi.SSID(i);
        if (netSsid.length() == 0) continue;
        jb.startArrObj(); jb.addStr("ssid", netSsid.c_str()); jb.addInt("rssi", WiFi.RSSI(i)); jb.endObj();
    }
    jb.endArr(); jb.endObj();
    WiFi.scanDelete();
    ap.portal->send(200, "application/json", jb.str());
}
