#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#ifndef RELEASE_BUILD
#include <ArduinoOTA.h>
#endif

#include <freertos/event_groups.h>
#include "config/Config.h"
#include "kernel/ServiceRegistry.h"
#include "kernel/SystemStatus.h"
#include "kernel/PowerManager.h"
#include "kernel/WiFiManager.h"
#include "kernel/ServerClient.h"
// HAL drivers: HX711/MPU6050/SSD1306 pure ESP-IDF.
// GPIO/Serial/Timer: keep Arduino for now (file still needs Arduino.h for WiFi/OTA/String).
#include "hal/HX711Driver.h"
#include "hal/MPU6050Driver.h"
#include "hal/SSD1306Driver.h"
#include "hal/Lcd1602Driver.h"
#include "hal/InterruptManager.h"
#include "hal/RtcDriver.h"
#include "hal/FingerprintDriver.h"
#include "data/StorageManager.h"
#include "data/ToolRepository.h"
#include "data/UserRepository.h"
#include "data/LogRepository.h"
#include "domain/services/WeightService.h"
#include "domain/services/MotionService.h"
#include "domain/services/StateManager.h"
#include "domain/services/AccessController.h"
#include "domain/services/DoorService.h"
#include "presentation/WebServer.h"
#include "presentation/DisplayManager.h"
#ifndef RELEASE_BUILD
#include "presentation/SerialCLI.h"
#endif
#include "utils/LogManager.h"
#ifndef RELEASE_BUILD
#include <ESPmDNS.h>
#endif

// ---- Dual-core boot sync ----
#define BOOT_I2C_DONE    (1 << 0)  // Core 0: MPU6050 + Display complete
#define BOOT_FP_GO       (1 << 1)  // Core 1: Core 0 may start Fingerprint
#define BOOT_FP_DONE     (1 << 2)  // Core 0: Fingerprint complete

// Forward declared — defined after globals + initWithRetry template
static void bootCore0Worker(void* arg);

// Task handles — stored in registry SCBs, but we keep local refs for creation
TaskHandle_t weightTaskHandle = NULL;
TaskHandle_t motionTaskHandle = NULL;
TaskHandle_t stateTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t accessTaskHandle = NULL;

// HAL drivers — live in registry HAL pool
HX711Driver hx711(PIN_HX711_DT, PIN_HX711_SCK, PIN_HX711_DRDY);
MPU6050Driver mpu;
SSD1306Driver display(PIN_DISPLAY_SDA, PIN_DISPLAY_SCL, PIN_DISPLAY_RST);
Lcd1602Driver lcdDisplay;

// Storage (NVS wrapper) — lives on stack, heap-allocates Preferences internally (unavoidable)
StorageManager storage;

// PowerManager + WiFiManager — need to persist for loop() access
PowerManager powerManager;
WiFiManager wifiManager;

// Access control hardware
FingerprintDriver fpDriver;
ServerClient serverClient;

#ifndef RELEASE_BUILD
SerialCLI cli;
#endif

// Task priorities
const int PRIORITY_WEIGHT = 8;
const int PRIORITY_MOTION = 8;
const int PRIORITY_STATE = 10;
const int PRIORITY_ACCESS = 9;
const int PRIORITY_WEB = 5;
const int PRIORITY_DISPLAY = 3;
const int PRIORITY_WIFI = 6;

// ============= INIT HELPERS =============

template<typename F>
bool initWithRetry(const char* name, F initFn,
                   const char* errorMsg, int maxRetries = 3, int baseDelayMs = 200) {
    auto* ss = g_registry.getSystemStatus();
    LOG_INFO("INIT", "Initializing %s (max %d retries)...", name, maxRetries);

    for (int attempt = 0; attempt <= maxRetries; attempt++) {
        if (initFn()) {
            if (attempt > 0) {
                LOG_INFO("INIT", "%s: OK (recovered on retry %d/%d)", name, attempt, maxRetries);
            } else {
                LOG_INFO("INIT", "%s: OK", name);
            }
            ss_markOK(ss, name);
            return true;
        }

        if (attempt < maxRetries) {
            // Adaptive backoff: 1×, 2×, 4× base delay (capped at 2s)
            int delayMs = baseDelayMs * (1 << attempt);
            if (delayMs > 2000) delayMs = 2000;
            LOG_WARN("INIT", "%s: FAILED — retry %d/%d in %dms...",
                     name, attempt + 1, maxRetries, delayMs);
            delay(delayMs); yield();
        }
    }

    char disabledMsg[128];
    snprintf(disabledMsg, sizeof(disabledMsg), "DISABLED (exceeded %d retries) — %s",
             maxRetries, errorMsg);
    ss_markError(ss, name, disabledMsg);
    LOG_ERROR("INIT", "%s: %s", name, disabledMsg);
    return false;
}

// ---- I2C address probe (no library needed — uses Wire) ----
static bool i2cProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// ---- Dual-core boot worker (Core 0) ----
// Runs I2C sensors then Fingerprint in parallel with Core 1's init.
static void bootCore0Worker(void* arg) {
    EventGroupHandle_t sync = (EventGroupHandle_t)arg;

    // Phase 1: I2C sensors (parallel with Core 1's Storage+HX711)
    initWithRetry("MPU6050", [](){ return mpu.begin(); },
        "I2C address 0x68 not found - check wiring", 3, 200);

    // Auto-detect display: probe LCD (0x27, 0x3F) then SSD1306 (0x3C)
    {
        auto* dm = g_registry.getDisplayManager();
        bool hasLCD = i2cProbe(LCD1602_ADDR_1) || i2cProbe(LCD1602_ADDR_2);
        bool hasOLED = i2cProbe(DISPLAY_ADDR);

        if (hasLCD) {
            LOG_INFO("INIT", "LCD 16x2 detected on I2C");
            initWithRetry("Display", [](){
                uint8_t addr = i2cProbe(LCD1602_ADDR_1) ? LCD1602_ADDR_1 : LCD1602_ADDR_2;
                return lcdDisplay.init(addr);
            }, "LCD not responding on 0x27 or 0x3F", 2, 300);
            dm->displayType = static_cast<uint8_t>(DisplayType::LCD1602);
        } else if (hasOLED) {
            LOG_INFO("INIT", "SSD1306 OLED detected on I2C");
            initWithRetry("Display", [](){
                display.init();
                delay(50);
                return display.isInitialized();
            }, "I2C address 0x3C not found - check wiring", 2, 300);
            dm->displayType = static_cast<uint8_t>(DisplayType::SSD1306);
        } else {
            LOG_ERROR("INIT", "No display detected on I2C bus");
            ss_markError(g_registry.getSystemStatus(), "Display",
                "No I2C display found — check wiring");
            dm->displayType = static_cast<uint8_t>(DisplayType::SSD1306);
        }
    }

    xEventGroupSetBits(sync, BOOT_I2C_DONE);

    // Wait for Core 1 signal to start Fingerprint
    xEventGroupWaitBits(sync, BOOT_FP_GO, pdFALSE, pdTRUE, portMAX_DELAY);

    // Phase 2: Fingerprint (parallel with Core 1's Door+Server+Access)
    initWithRetry("Fingerprint", [](){ return fpDriver.begin(); },
        "No sensor on UART2 (RX=5, TX=4) - check wiring", 3, 300);

    xEventGroupSetBits(sync, BOOT_FP_DONE);
    vTaskDelete(NULL);
}

// Dynamic task delay — reads operational mode from registry
inline TickType_t opDelay(int apMs, int staMs) {
    return (ss_getOperationalMode(g_registry.getSystemStatus()) == OperationalMode::OP_AP_FULL)
        ? pdMS_TO_TICKS(apMs)
        : pdMS_TO_TICKS(staMs);
}

// ============= TASKS =============

#define SENSOR_RETRY_COUNT 10
#define SENSOR_RETRY_DELAY_MS 5000

void weightTask(void* param) {
    auto* mem = g_registry.getWeightService();
    auto* ss = g_registry.getSystemStatus();

    if (ss_getStatus(ss, "HX711") == ComponentStatus::ERROR) {
        for (int retry = 1; retry <= SENSOR_RETRY_COUNT; retry++) {
            LOG_WARN("WEIGHT", "HX711 init failed, retry %d/%d", retry, SENSOR_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
            hx711.begin();
            delay(100);
            int32_t testRead = hx711.readRaw();
            if (testRead != INT32_MIN && testRead != 0) {
                ss_markOK(ss, "HX711");
                LOG_INFO("WEIGHT", "HX711 recovered on retry %d", retry);
                goto weight_start;
            }
        }
        LOG_ERROR("WEIGHT", "HX711 unrecoverable after %d retries", SENSOR_RETRY_COUNT);
        ss_markError(ss, "HX711", "Unrecoverable — task exiting");
        vTaskDelete(NULL);
        return;
    }
weight_start:
    while (true) {
        if (InterruptManager::isHX711Ready()) {
            int32_t raw = hx711.readRaw();
            if (raw != INT32_MIN) {
                ws_onRawReading(mem, raw);
            }
        }
        ws_update(mem);  // drains mailbox + processes filter

        g_registry.sendCmd(ServiceId::POWER,
            static_cast<uint8_t>(KernelMsgType::ACTIVITY));
        g_registry.heartbeat(ServiceId::WEIGHT_SERVICE);
        vTaskDelay(opDelay(TaskRate::AP_WEIGHT_MS, TaskRate::STA_WEIGHT_MS));
    }
}

void motionTask(void* param) {
    auto* mem = g_registry.getMotionService();
    auto* ss = g_registry.getSystemStatus();

    if (ss_getStatus(ss, "MPU6050") == ComponentStatus::ERROR) {
        for (int retry = 1; retry <= SENSOR_RETRY_COUNT; retry++) {
            LOG_WARN("MOTION", "MPU6050 init failed, retry %d/%d", retry, SENSOR_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
            if (mpu.begin()) {
                ss_markOK(ss, "MPU6050");
                LOG_INFO("MOTION", "MPU6050 recovered on retry %d", retry);
                goto motion_start;
            }
        }
        LOG_ERROR("MOTION", "MPU6050 unrecoverable after %d retries", SENSOR_RETRY_COUNT);
        ss_markError(ss, "MPU6050", "Unrecoverable — task exiting");
        vTaskDelete(NULL);
        return;
    }
motion_start:
    while (true) {
        if (InterruptManager::isMPUTriggered()) {
            ms_update(mem, &mpu);  // reads accel, sends MOTION_DETECTED to StateManager

            g_registry.sendCmd(ServiceId::POWER,
                static_cast<uint8_t>(KernelMsgType::MOTION_WAKE));
        }
        g_registry.heartbeat(ServiceId::MOTION_SERVICE);
        vTaskDelay(opDelay(TaskRate::AP_MOTION_MS, TaskRate::STA_MOTION_MS));
    }
}

void stateTask(void* param) {
    auto* mem = g_registry.getStateManager();
    QueueHandle_t q = g_registry.scb[static_cast<uint8_t>(ServiceId::STATE_MANAGER)].inbox;

    if (!q) {
        LOG_ERROR("STATE", "No STATE_MANAGER mailbox — task exiting");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        ServiceMessage msg;
        // Drain pending messages
        while (xQueueReceive(q, &msg, 0) == pdTRUE) {
            sm_dispatchMessage(mem, msg);
        }

        sm_updatePeriodic(mem);

        // Block with timeout
        TickType_t delayTicks = opDelay(TaskRate::AP_STATE_MS, TaskRate::STA_STATE_MS);
        if (xQueueReceive(q, &msg, delayTicks) == pdTRUE) {
            sm_dispatchMessage(mem, msg);
            while (xQueueReceive(q, &msg, 0) == pdTRUE) {
                sm_dispatchMessage(mem, msg);
            }
        }
        sm_updatePeriodic(mem);
        g_registry.heartbeat(ServiceId::STATE_MANAGER);
    }
}

void webTask(void* param) {
    auto* ss = g_registry.getSystemStatus();
    if (ss_getStatus(ss, "WebServer") == ComponentStatus::ERROR) {
        LOG_INFO("INIT", "WebServer not available, task exiting");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        web_handle();
        vTaskDelay(opDelay(TaskRate::AP_WEB_MS, TaskRate::STA_WEB_MS));
    }
}

void displayTask(void* param) {
    auto* ss = g_registry.getSystemStatus();
    if (ss_getStatus(ss, "Display") == ComponentStatus::ERROR) {
        LOG_INFO("INIT", "Display not available, task exiting");
        vTaskDelete(NULL);
        return;
    }

    auto* mem = g_registry.getDisplayManager();
    while (true) {
        ServiceMessage msg;
        while (g_registry.tryReceive(ServiceId::DISPLAY_MANAGER, msg)) {
            dm_dispatchMessage(mem, msg);
        }

        dm_update(mem, &display, &lcdDisplay);

        TickType_t delayTicks = opDelay(TaskRate::AP_DISPLAY_MS, TaskRate::STA_DISPLAY_MS);
        vTaskDelay(delayTicks);
    }
}

void wifiTask(void* param) {
    while (true) {
        wifiManager.update();

        if (!wifiManager.isAPMode()) {
#ifndef RELEASE_BUILD
            ArduinoOTA.handle();
            cli.handle();
#endif
        }

        vTaskDelay(opDelay(TaskRate::AP_WIFI_MS, TaskRate::STA_WIFI_MS));
    }
}

void accessTask(void* param) {
    auto* ss = g_registry.getSystemStatus();
    if (ss_getStatus(ss, "Fingerprint") == ComponentStatus::ERROR) {
        LOG_INFO("ACCESS", "Fingerprint sensor not available, task exiting");
        vTaskDelete(NULL);
        return;
    }

    auto* acMem = g_registry.getAccessController();
    auto* dsMem = g_registry.getDoorService();

    while (true) {
        ServiceMessage msg;
        while (g_registry.tryReceive(ServiceId::ACCESS_CONTROLLER, msg)) {
            ac_dispatchMessage(acMem, msg, &fpDriver, &serverClient, dsMem);
        }
        while (g_registry.tryReceive(ServiceId::DOOR_SERVICE, msg)) {
            ds_dispatchMessage(dsMem, msg);
        }

        ac_update(acMem, &fpDriver, &serverClient, dsMem);
        ds_update(dsMem);
        serverClient.update();
        vTaskDelay(opDelay(TaskRate::AP_ACCESS_MS, TaskRate::STA_ACCESS_MS));
    }
}

// ============= SETUP =============

void setup() {
    // Boot blink
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, LOW);  delay(100); yield();
        digitalWrite(PIN_LED, HIGH); delay(100); yield();
    }
    digitalWrite(PIN_LED, LOW);
    delay(100);
    digitalWrite(PIN_LED, HIGH);

    // Force AP mode if BOOT button held
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    if (digitalRead(PIN_BUTTON) == LOW) {
        for (int i = 0; i < 5; i++) {
            digitalWrite(PIN_LED, LOW);  delay(50);
            digitalWrite(PIN_LED, HIGH); delay(50);
        }
        storage.begin();
        storage.remove("wifi_ssid");
        storage.remove("wifi_pass");
        digitalWrite(PIN_LED, LOW);
    }

    Serial.begin(115200);
    delay(10);
    logInit(2, 2560);
    delay(10);

    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "ESP32 Inventory Box Starting...");
    LOG_INFO("INIT", "===========================================");

    // ---- INIT REGISTRY ----
    LOG_INFO("INIT", "Initializing Service Registry...");
    g_registry.init();

    // ---- INIT SYSTEM STATUS ----
    ss_begin(g_registry.getSystemStatus());
    ss_setBootStage(g_registry.getSystemStatus(), BootStage::BS_STORAGE);

    // ---- PRE-INIT I2C PULLUPS ----
    pinMode(PIN_MPU_SDA, INPUT_PULLUP);
    pinMode(PIN_MPU_SCL, INPUT_PULLUP);

    // ---- DUAL-CORE BOOT: parallel sensor init ----
    EventGroupHandle_t bootSync = xEventGroupCreate();
    xTaskCreatePinnedToCore(bootCore0Worker, "bootCore0", 3072,
                            bootSync, 12, NULL, 0);
    LOG_INFO("INIT", "Core 0 worker launched (I2C sensors)");

    // === Core 1: Storage + HX711 (parallel with Core 0's I2C sensors) ===
    // ---- STORAGE ----
    initWithRetry("Storage", [&](){ return storage.begin(); },
        "NVS initialization failed", 0);

    // ---- RTC ----
    if (!initWithRetry("RTC", [](){ return rtc_init(); },
        "DS3231 not found on I2C_NUM_1 (pins 18/23)", 3, 1000)) {
        rtc_setFallbackTime();
        LOG_INFO("INIT", "RTC: using compile-time + uptime fallback");
    }

    ss_setBootStage(g_registry.getSystemStatus(), BootStage::BS_HX711);

    // ---- HX711 ----
    initWithRetry("HX711", [&](){
        hx711.begin();
        delay(50);
        int32_t testRead = hx711.readRaw();
        return (testRead != INT32_MIN && testRead != 0);
    }, "No response - check wiring (DT=16, SCK=17)", 3, 300);

    // Wait for Core 0 to finish I2C sensors
    ss_setBootStage(g_registry.getSystemStatus(), BootStage::BS_MPU6050);
    ss_setBootStage(g_registry.getSystemStatus(), BootStage::BS_DISPLAY);
    xEventGroupWaitBits(bootSync, BOOT_I2C_DONE, pdTRUE, pdTRUE, portMAX_DELAY);
    LOG_INFO("INIT", "Phase 1 complete (all sensors)");

    // ---- INTERRUPTS ----
    LOG_INFO("INIT", "Configuring interrupts...");
    InterruptManager::begin();
    LOG_INFO("INIT", "Interrupts: OK");

    auto* ss = g_registry.getSystemStatus();
    if (ss_getStatus(ss, "HX711") == ComponentStatus::ERROR) {
        gpio_isr_handler_remove((gpio_num_t)PIN_HX711_DRDY);
        gpio_set_direction((gpio_num_t)PIN_HX711_DRDY, GPIO_MODE_DISABLE);
        LOG_INFO("INIT", "HX711 interrupt disabled (sensor offline)");
    }
    if (ss_getStatus(ss, "MPU6050") == ComponentStatus::ERROR) {
        gpio_isr_handler_remove((gpio_num_t)PIN_MPU_INT);
        gpio_set_direction((gpio_num_t)PIN_MPU_INT, GPIO_MODE_DISABLE);
        LOG_INFO("INIT", "MPU6050 interrupt disabled (sensor offline)");
    }

    // ---- REGISTER MAILBOXES ----
    LOG_INFO("INIT", "Registering service mailboxes...");
    g_registry.registerMailbox(ServiceId::STATE_MANAGER,    32);
    g_registry.registerMailbox(ServiceId::WEIGHT_SERVICE,   8);
    g_registry.registerMailbox(ServiceId::ACCESS_CONTROLLER, 16);
    g_registry.registerMailbox(ServiceId::DOOR_SERVICE,     8);
    g_registry.registerMailbox(ServiceId::DISPLAY_MANAGER,  16);
    g_registry.registerMailbox(ServiceId::POWER,            8);
    g_registry.registerMailbox(ServiceId::MOTION_SERVICE,   4);

    // ---- INIT SERVICES (from registry memory) ----
    LOG_INFO("INIT", "Initializing services...");

    // WeightService
    auto* wm = g_registry.getWeightService();
    wm->calibrationFactor = Config::CALIBRATION_FACTOR;
    wm->filterSize = Config::FILTER_SIZE;
    hx711.begin();

    if (ss_getStatus(ss, "MPU6050") != ComponentStatus::ERROR) {
        auto* mm = g_registry.getMotionService();
        mm->initialized = 1;
        mpu.readAccel(mm->restingAccel[0], mm->restingAccel[1], mm->restingAccel[2]);
        mm->restingAccel[2] -= 1.0f;
    } else {
        LOG_INFO("INIT", "MotionService: SKIPPED (MPU6050 disabled)");
    }

    // Load saved baseline
    float savedBaseline = storage.getFloat("baseline", 0.0f);
    if (savedBaseline > 0) {
        wm->baseline = savedBaseline;
        g_registry.getStateManager()->baselineGrams = savedBaseline;
        LOG_INFO("INIT", "Loaded baseline: %.1f g", savedBaseline);
    }

    // Init StateManager
    auto* sm = g_registry.getStateManager();
    sm_init(sm, &storage);

    // Init repositories
    tr_init(g_registry.getToolRepository(), &storage);
    // ur_init, lr_init...

    // Init PowerManager
    powerManager.setBaseline(savedBaseline);
    if (ss_getStatus(ss, "MPU6050") != ComponentStatus::ERROR) {
        powerManager.begin(true);
    } else {
        powerManager.begin(false);
    }

    ss_setBootStage(ss, BootStage::BS_WIFI);

    // ---- WIFI ----
    LOG_INFO("INIT", "Initializing WiFi...");
    wifiManager.begin();
    LOG_INFO("INIT", "WiFi init complete");

    if (wifiManager.isAPMode()) {
        ss_markWarning(ss, "WiFi", "No credentials stored - AP mode active");
        LOG_INFO("INIT", "WiFi: WARNING - AP mode (no credentials)");
    } else if (wifiManager.isConnected()) {
        ss_markOK(ss, "WiFi");
        LOG_INFO("INIT", "WiFi: OK - %s (%d dBm)", wifiManager.getSSID(), wifiManager.getRSSI());
    } else {
        ss_markError(ss, "WiFi", "Connection failed");
        LOG_INFO("INIT", "WiFi: FAILED - Connection failed");
    }

    ss_setBootStage(ss, BootStage::BS_WEB_SERVER);

    // ---- WEB SERVER ----
    if (!wifiManager.isAPMode()) {
        LOG_INFO("INIT", "Starting Web Server...");
        web_begin();
        ss_markOK(ss, "WebServer");
        LOG_INFO("INIT", "WebServer: OK");
    } else {
        LOG_INFO("INIT", "WebServer: SKIPPED (AP mode - configPortal active)");
    }

    ss_setBootStage(ss, BootStage::BS_FINGERPRINT);

    // ---- FINGERPRINT (runs on Core 0 in parallel with WiFi) ----
    // Signal Core 0 to start fingerprint init
    xEventGroupSetBits(bootSync, BOOT_FP_GO);

    // ---- DOOR SERVICE ----
    ds_begin(g_registry.getDoorService());
    ss_markOK(ss, "Door");
    LOG_INFO("INIT", "Door: OK");

    // ---- SERVER CLIENT ----
    String serverUrl = storage.getString("cfg_server_url", "");
    String serverToken = storage.getString("cfg_server_token", "");
    if (serverUrl.length() > 0) {
        serverClient.begin(serverUrl.c_str(), serverToken.length() > 0 ? serverToken.c_str() : nullptr);
        ss_markOK(ss, "ServerClient");
    } else {
        ss_markWarning(ss, "ServerClient", "No server URL configured");
    }

    // ---- ACCESS CONTROLLER ----
    auto* ac = g_registry.getAccessController();
    ac_init(ac);
    ss_markOK(ss, "AccessController");
    LOG_INFO("INIT", "AccessController: OK");

    // ---- DISPLAY MANAGER ----
    if (ss_getStatus(ss, "Display") != ComponentStatus::ERROR) {
        auto* dm = g_registry.getDisplayManager();
        bool isLCD = (dm->displayType == static_cast<uint8_t>(DisplayType::LCD1602));
        dm->healthy = isLCD ? lcdDisplay.isInitialized() : display.isInitialized();
        dm->awake = true;
        dm->currentScreen = 0;
        LOG_INFO("INIT", "DisplayManager: %s heap=%d",
                 isLCD ? "LCD 16x2" : "SSD1306", ESP.getFreeHeap());
    } else {
        LOG_INFO("INIT", "DisplayManager: SKIPPED (display offline)");
    }

#ifndef RELEASE_BUILD
    // ---- mDNS ----
    if (MDNS.begin("inventory-box")) {
        MDNS.addService("http", "tcp", 80);
        LOG_INFO("INIT", "mDNS: http://inventory-box.local");
    }

    // ---- OTA ----
    ArduinoOTA.setHostname("inventory-box");
    ArduinoOTA.onStart([]() { LOG_INFO("OTA", "Update starting..."); });
    ArduinoOTA.onEnd([]() { LOG_INFO("OTA", "Update complete, rebooting"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {});
    ArduinoOTA.onError([](ota_error_t error) { LOG_ERROR("OTA", "Error[%u]", error); });
    ArduinoOTA.begin();
    LOG_INFO("INIT", "OTA ready");

    cli.begin();
#endif

    // Print connection info
    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    if (wifiManager.isAPMode()) {
        LOG_INFO("INIT", "WiFi AP MODE ACTIVE");
        LOG_INFO("INIT", "SSID: %s", wifiManager.getSSID());
        LOG_INFO("INIT", "AP IP: %s", wifiManager.getIP());
        LOG_INFO("INIT", "Connect to configure WiFi credentials");
    } else {
        LOG_INFO("INIT", "WiFi Connected!");
        LOG_INFO("INIT", "IP Address: %s", wifiManager.getIP());
        LOG_INFO("INIT", "Signal: %d dBm", wifiManager.getRSSI());
    }

    // Component status summary (uses registry)
    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "COMPONENT STATUS:");
    LOG_INFO("INIT", "-------------------------------------------");
    LOG_INFO("INIT", "Storage:     %s", ss_getStatus(ss, "Storage") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "HX711:       %s", ss_getStatus(ss, "HX711") == ComponentStatus::OK ? "[OK]" :
        ss_getStatus(ss, "HX711") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "MPU6050:     %s", ss_getStatus(ss, "MPU6050") == ComponentStatus::OK ? "[OK]" :
        ss_getStatus(ss, "MPU6050") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "Display:     %s", ss_getStatus(ss, "Display") == ComponentStatus::OK ? "[OK]" :
        ss_getStatus(ss, "Display") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "WiFi:        %s", wifiManager.isConnected() ? "[OK]" :
        wifiManager.isAPMode() ? "[AP]" : "[FAIL]");
    LOG_INFO("INIT", "WebServer:   %s", ss_getStatus(ss, "WebServer") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "Fingerprint: %s", ss_getStatus(ss, "Fingerprint") == ComponentStatus::OK ? "[OK]" :
        ss_getStatus(ss, "Fingerprint") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "Door:        %s", ss_getStatus(ss, "Door") == ComponentStatus::OK ? "[OK]" : "[---]");
    LOG_INFO("INIT", "Server:      %s", ss_getStatus(ss, "ServerClient") == ComponentStatus::OK ? "[OK]" :
        ss_getStatus(ss, "ServerClient") == ComponentStatus::WARNING ? "[WARN]" : "[---]");
    LOG_INFO("INIT", "AccessCtrl:  %s", ss_getStatus(ss, "AccessController") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "===========================================");

    if (ss_hasErrors(ss)) {
        LOG_INFO("INIT", "WARNING: Some components failed to initialize!");
        LOG_INFO("INIT", "Last error: %s", ss_getLastError(ss));
        LOG_INFO("INIT", "System will continue with reduced functionality");
        LOG_INFO("INIT", "===========================================");
    }

    // Wait for Core 0 fingerprint init to complete
    xEventGroupWaitBits(bootSync, BOOT_FP_DONE, pdTRUE, pdTRUE, portMAX_DELAY);
    vEventGroupDelete(bootSync);
    LOG_INFO("INIT", "Phase 2 complete (network + fingerprint)");

    ss_setBootStage(ss, BootStage::BS_ACCESS_SERVER);

    // ---- CREATE TASKS ----
    LOG_INFO("INIT", "Creating tasks...");

    int tasksOk = 0, tasksFail = 0;
    // Static stack allocator (WiFi stays heap — needs large TCP/IP stack)
    static size_t stackOff = 0;
    static int tcbIdx = 0;
    #define CREATE_TASK_STATIC(fn, name, stackWords, prio, handle, core) do { \
        StackType_t* stk = (StackType_t*)&g_registry.taskStackPool[stackOff]; \
        StaticTask_t* tcb = &g_registry.taskTCBs[tcbIdx++]; \
        stackOff += (stackWords) * sizeof(StackType_t); \
        TaskHandle_t th = xTaskCreateStaticPinnedToCore(fn, name, stackWords, NULL, prio, stk, tcb, core); \
        if (th == NULL) { \
            LOG_ERROR("INIT", "FAILED to create %s task", name); \
            tasksFail++; \
        } else { \
            tasksOk++; \
            *(handle) = th; \
        } \
    } while(0)
    #define CREATE_TASK_HEAP(fn, name, stack, prio, handle, core) do { \
        if (xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, handle, core) != pdPASS) { \
            LOG_ERROR("INIT", "FAILED to create %s task", name); \
            tasksFail++; \
        } else { tasksOk++; } \
    } while(0)

    // WiFi stays heap (needs large stack for lwIP)
    CREATE_TASK_HEAP(wifiTask, "WiFi", 6144, PRIORITY_WIFI, &wifiTaskHandle, 0);
    // Domain tasks use static stacks
    CREATE_TASK_STATIC(stateTask, "State", 1536, PRIORITY_STATE, &stateTaskHandle, 0);

    if (ss_getStatus(ss, "Fingerprint") != ComponentStatus::ERROR) {
        CREATE_TASK_STATIC(accessTask, "Access", 2560, PRIORITY_ACCESS, &accessTaskHandle, 0);
    }
    if (ss_getStatus(ss, "HX711") != ComponentStatus::ERROR) {
        CREATE_TASK_STATIC(weightTask, "Weight", 1536, PRIORITY_WEIGHT, &weightTaskHandle, 0);
    }
    if (ss_getStatus(ss, "MPU6050") != ComponentStatus::ERROR) {
        CREATE_TASK_STATIC(motionTask, "Motion", 1536, PRIORITY_MOTION, &motionTaskHandle, 0);
    }
    if (ss_getStatus(ss, "WebServer") == ComponentStatus::OK) {
        CREATE_TASK_STATIC(webTask, "Web", 2560, PRIORITY_WEB, &webTaskHandle, 0);
    }
    if (ss_getStatus(ss, "Display") != ComponentStatus::ERROR) {
        CREATE_TASK_STATIC(displayTask, "Display", 1536, PRIORITY_DISPLAY, &displayTaskHandle, 1);
    }

    LOG_INFO("INIT", "Tasks created: %d OK, %d failed", tasksOk, tasksFail);

    ss_setBootComplete(ss);
    LOG_INFO("INIT", "Free heap: %d bytes", ESP.getFreeHeap());
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "System ready! Registry at 0x%08X", (unsigned int)(uintptr_t)&g_registry);
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "setup() complete");
}

// ============= MAIN LOOP =============

void loop() {
    bool apMode = wifiManager.isAPMode();

#ifndef RELEASE_BUILD
    if (!apMode) {
        ArduinoOTA.handle();
        cli.handle();
    }
#endif
    // Drain PowerManager mailbox
    {
        ServiceMessage pmMsg;
        while (g_registry.tryReceive(ServiceId::POWER, pmMsg)) {
            switch (static_cast<KernelMsgType>(pmMsg.type)) {
                case KernelMsgType::ACTIVITY:
                    powerManager.onActivity();
                    break;
                case KernelMsgType::MOTION_WAKE:
                    powerManager.handleWakeFromMotion();
                    break;
                default:
                    break;
            }
        }
    }

    powerManager.update();

    // Heartbeat
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        auto* ss = g_registry.getSystemStatus();
        LOG_INFO("SYS", "heartbeat heap=%d state=0 mode=%s reg=0x%08X",
                 ESP.getFreeHeap(),
                 (ss_getOperationalMode(ss) == OperationalMode::OP_AP_FULL) ? "AP" : "STA",
                 (unsigned int)(uintptr_t)&g_registry);
        lastHeartbeat = millis();
    }

    // Status LED
    static unsigned long lastBlink = 0;
    bool connected = wifiManager.isConnected();
    bool hasErrors = ss_hasErrors(g_registry.getSystemStatus());

    if (apMode) {
        digitalWrite(PIN_LED, LOW);
    } else if (hasErrors) {
        if (millis() - lastBlink > 200) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            lastBlink = millis();
        }
    } else if (connected) {
        if (millis() - lastBlink > 3000) {
            digitalWrite(PIN_LED, LOW); delay(50);
            digitalWrite(PIN_LED, HIGH);
            lastBlink = millis();
        }
    } else {
        if (millis() - lastBlink > 500) {
            digitalWrite(PIN_LED, !digitalRead(PIN_LED));
            lastBlink = millis();
        }
    }

    delay(10);
}
