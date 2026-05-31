#include <Arduino.h>
#include <WiFi.h>
#ifndef RELEASE_BUILD
#include <ArduinoOTA.h>
#endif

#include "config/Config.h"
#include "hal/HX711Driver.h"
#include "hal/MPU6050Driver.h"
#include "hal/SSD1306Driver.h"
#include "hal/InterruptManager.h"
#include "hal/FingerprintDriver.h"

#include "data/StorageManager.h"
#include "data/ToolRepository.h"
#include "data/UserRepository.h"
#include "data/LogRepository.h"

#include "domain/events/EventBus.h"
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
#include "kernel/PowerManager.h"
#include "kernel/WiFiManager.h"
#include "kernel/SystemStatus.h"
#include "kernel/ServerClient.h"
#include "utils/LogManager.h"
#ifndef RELEASE_BUILD
#include <ESPmDNS.h>
#endif
#include <functional>

// Task handles
TaskHandle_t weightTaskHandle = NULL;
TaskHandle_t motionTaskHandle = NULL;
TaskHandle_t stateTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t accessTaskHandle = NULL;

// Global instances
StorageManager storage;
HX711Driver hx711(PIN_HX711_DT, PIN_HX711_SCK, PIN_HX711_DRDY);
MPU6050Driver mpu;
SSD1306Driver display(PIN_DISPLAY_SDA, PIN_DISPLAY_SCL, PIN_DISPLAY_RST);

ToolRepository toolRepo(&storage);
UserRepository userRepo(&storage);
LogRepository logRepo;

EventBus* eventBus = EventBus::getInstance();
WeightService weightService(&hx711);
MotionService motionService(&mpu);
StateManager stateManager(eventBus);

WebServerManager webServer(eventBus);
PowerManager powerManager;
WiFiManager wifiManager;
SystemStatus& systemStatus = SystemStatus::getInstance();
DisplayManager displayManager(&display, eventBus);

// Access control
FingerprintDriver fpDriver;
ServerClient serverClient;
AccessController accessController(eventBus);
DoorService doorService(eventBus);
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

/**
 * Initialize a component with retry on failure.
 * On success: markOK. On retry exhaustion: markError with "DISABLED" prefix.
 * Returns true if component initialized successfully.
 */
bool initWithRetry(const char* name, std::function<bool()> initFn,
                   const char* errorMsg, int maxRetries = 3, int retryDelayMs = 1000) {
    LOG_INFO("INIT", "Initializing %s (max %d retries)...", name, maxRetries);

    for (int attempt = 0; attempt <= maxRetries; attempt++) {
        if (initFn()) {
            if (attempt > 0) {
                LOG_INFO("INIT", "%s: OK (recovered on retry %d/%d)", name, attempt, maxRetries);
            } else {
                LOG_INFO("INIT", "%s: OK", name);
            }
            systemStatus.markOK(name);
            return true;
        }

        if (attempt < maxRetries) {
            LOG_WARN("INIT", "%s: FAILED — retry %d/%d in %dms...",
                     name, attempt + 1, maxRetries, retryDelayMs);
            delay(retryDelayMs); yield();
        }
    }

    // All retries exhausted — permanently disable
    char disabledMsg[128];
    snprintf(disabledMsg, sizeof(disabledMsg), "DISABLED (exceeded %d retries) — %s",
             maxRetries, errorMsg);
    systemStatus.markError(name, disabledMsg);
    LOG_ERROR("INIT", "%s: %s", name, disabledMsg);
    return false;
}

// Dynamic task delay — reads operational mode
inline TickType_t opDelay(int apMs, int staMs) {
    return (SystemStatus::getInstance().getOperationalMode() == OperationalMode::OP_AP_FULL)
        ? pdMS_TO_TICKS(apMs)
        : pdMS_TO_TICKS(staMs);
}

// ============= TASKS =============

#define SENSOR_RETRY_COUNT 10
#define SENSOR_RETRY_DELAY_MS 5000

void weightTask(void* param) {
    // Retry loop if HX711 failed at boot
    if (systemStatus.getStatus("HX711") == ComponentStatus::ERROR) {
        for (int retry = 1; retry <= SENSOR_RETRY_COUNT; retry++) {
            LOG_WARN("WEIGHT", "HX711 init failed, retry %d/%d", retry, SENSOR_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
            hx711.begin();
            delay(100);
            int32_t testRead = hx711.readRaw();
            if (testRead != INT32_MIN && testRead != 0) {
                systemStatus.markOK("HX711");
                LOG_INFO("WEIGHT", "HX711 recovered on retry %d", retry);
                goto weight_start;
            }
        }
        LOG_ERROR("WEIGHT", "HX711 unrecoverable after %d retries", SENSOR_RETRY_COUNT);
        systemStatus.markError("HX711", "Unrecoverable — task exiting");
        vTaskDelete(NULL);
        return;
    }
weight_start:
    while (true) {
        if (InterruptManager::isHX711Ready()) {
            int32_t raw = hx711.readRaw();
            if (raw != INT32_MIN) {
                weightService.onRawReading(raw);
            }
        }
        weightService.update();
        powerManager.onActivity();
        vTaskDelay(opDelay(TaskRate::AP_WEIGHT_MS, TaskRate::STA_WEIGHT_MS));
    }
}

void motionTask(void* param) {
    // Retry loop if MPU6050 failed at boot
    if (systemStatus.getStatus("MPU6050") == ComponentStatus::ERROR) {
        for (int retry = 1; retry <= SENSOR_RETRY_COUNT; retry++) {
            LOG_WARN("MOTION", "MPU6050 init failed, retry %d/%d", retry, SENSOR_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_DELAY_MS));
            if (mpu.begin()) {
                systemStatus.markOK("MPU6050");
                LOG_INFO("MOTION", "MPU6050 recovered on retry %d", retry);
                goto motion_start;
            }
        }
        LOG_ERROR("MOTION", "MPU6050 unrecoverable after %d retries", SENSOR_RETRY_COUNT);
        systemStatus.markError("MPU6050", "Unrecoverable — task exiting");
        vTaskDelete(NULL);
        return;
    }
motion_start:
    while (true) {
        if (InterruptManager::isMPUTriggered()) {
            motionService.update();
            MotionType motion = motionService.getCurrentMotion();
            stateManager.onMotionDetected(motion);
            if (powerManager.getCurrentState() == PM_DEEP_SLEEP) {
                powerManager.handleWakeFromMotion();
            }
        }
        vTaskDelay(opDelay(TaskRate::AP_MOTION_MS, TaskRate::STA_MOTION_MS));
    }
}

void stateTask(void* param) {
    while (true) {
        stateManager.update();
        vTaskDelay(opDelay(TaskRate::AP_STATE_MS, TaskRate::STA_STATE_MS));
    }
}

void webTask(void* param) {
    // Check if WebServer is working
    if (systemStatus.getStatus("WebServer") == ComponentStatus::ERROR) {
        LOG_INFO("INIT", "WebServer not available, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        webServer.handle();
        vTaskDelay(opDelay(TaskRate::AP_WEB_MS, TaskRate::STA_WEB_MS));
    }
}

void displayTask(void* param) {
    if (systemStatus.getStatus("Display") == ComponentStatus::ERROR) {
        LOG_INFO("INIT", "Display not available, task exiting");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        displayManager.update();
        vTaskDelay(opDelay(TaskRate::AP_DISPLAY_MS, TaskRate::STA_DISPLAY_MS));
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
    if (systemStatus.getStatus("Fingerprint") == ComponentStatus::ERROR) {
        LOG_INFO("ACCESS", "Fingerprint sensor not available, task exiting");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        accessController.update();
        doorService.update();
        serverClient.update();
        vTaskDelay(opDelay(TaskRate::AP_ACCESS_MS, TaskRate::STA_ACCESS_MS));
    }
}

// ============= SETUP =============

void setup() {
    // Boot blink: 3 fast flashes = "I'm alive"
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED, LOW);  delay(100); yield();
        digitalWrite(PIN_LED, HIGH); delay(100); yield();
    }
    // Solid ON for 2 seconds — confirms stable boot
    digitalWrite(PIN_LED, LOW);
    delay(2000); yield();
    digitalWrite(PIN_LED, HIGH);

    // Hold BOOT button at power-on to force AP mode (clear WiFi credentials)
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    if (digitalRead(PIN_BUTTON) == LOW) {
        for (int i = 0; i < 5; i++) {
            digitalWrite(PIN_LED, LOW);  delay(50);
            digitalWrite(PIN_LED, HIGH); delay(50);
        }
        storage.begin();
        storage.remove("wifi_ssid");
        storage.remove("wifi_pass");
        digitalWrite(PIN_LED, LOW); // solid = AP forced
    }

    Serial.begin(115200);
    delay(100);
    logInit(2, 2560);
    delay(100);
    
    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "ESP32 Inventory Box Starting...");
    LOG_INFO("INIT", "===========================================");
    
    // Initialize system status
    systemStatus.begin();
    systemStatus.setBootStage(BootStage::BS_STORAGE);

    // ---- PRE-INIT I2C PULLUPS (prevent floating-line interrupt storms) ----
    pinMode(PIN_MPU_SDA, INPUT_PULLUP);
    pinMode(PIN_MPU_SCL, INPUT_PULLUP);
    delay(5);

    // ---- STORAGE ----
    initWithRetry("Storage", [&](){ return storage.begin(); },
        "NVS initialization failed", 0);  // No retry — NVS fatal if fails

    systemStatus.setBootStage(BootStage::BS_HX711);

    // ---- HX711 ----
    initWithRetry("HX711", [&](){
        hx711.begin();
        delay(100);
        int32_t testRead = hx711.readRaw();
        return (testRead != INT32_MIN && testRead != 0);
    }, "No response - check wiring (DT=16, SCK=17)", 3, 2000);
    
    systemStatus.setBootStage(BootStage::BS_MPU6050);
    
    // ---- MPU6050 ----
    initWithRetry("MPU6050", [&](){ return mpu.begin(); },
        "I2C address 0x68 not found - check wiring", 3, 1500);

    systemStatus.setBootStage(BootStage::BS_DISPLAY);

    // ---- DISPLAY ----
    initWithRetry("Display", [&](){
        display.init();
        delay(100);
        return display.isInitialized();
    }, "I2C address 0x3C not found - check wiring", 2, 2000);
    
    // ---- INTERRUPTS ----
    LOG_INFO("INIT", "Configuring interrupts...");
    InterruptManager::begin();
    LOG_INFO("INIT", "Interrupts: OK");

    // Disable interrupts for failed sensors (prevents storm on floating input-only pins)
    if (systemStatus.getStatus("HX711") == ComponentStatus::ERROR) {
        gpio_isr_handler_remove((gpio_num_t)PIN_HX711_DRDY);
        gpio_set_direction((gpio_num_t)PIN_HX711_DRDY, GPIO_MODE_DISABLE);
        LOG_INFO("INIT", "HX711 interrupt disabled (sensor offline)");
    }
    if (systemStatus.getStatus("MPU6050") == ComponentStatus::ERROR) {
        gpio_isr_handler_remove((gpio_num_t)PIN_MPU_INT);
        gpio_set_direction((gpio_num_t)PIN_MPU_INT, GPIO_MODE_DISABLE);
        LOG_INFO("INIT", "MPU6050 interrupt disabled (sensor offline)");
    }
    
    // ---- SERVICES ----
    LOG_INFO("INIT", "Initializing services...");
    weightService.begin();
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        motionService.begin();
    } else {
        LOG_INFO("INIT", "MotionService: SKIPPED (MPU6050 disabled)");
    }
    
    // Load saved baseline
    float savedBaseline = storage.getFloat("baseline", 0.0f);
    if (savedBaseline > 0) {
        weightService.setBaseline(savedBaseline);
        stateManager.setBaseline(savedBaseline);
        LOG_INFO("INIT", "Loaded baseline: %.1f g", savedBaseline);
    }
    
    // Calibrate motion baseline (don't fail if MPU6050 is down)
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        motionService.calibrateBaseline();
    }

    delay(50);  // Let logger task drain retry-spam from queue

    // Initialize managers
    stateManager.begin();
    stateManager.setWeightService(&weightService);
    stateManager.setMotionService(&motionService);
    stateManager.setToolRepository(&toolRepo);
    stateManager.setLogRepository(&logRepo);
    
    // Initialize power manager (skip wake sources if MPU6050 disabled — GPIO35 floats)
    LOG_INFO("INIT", "powerManager...");
    powerManager.setBaseline(savedBaseline);
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        powerManager.begin(true);
    } else {
        // Skip setupWakeSources() — GPIO35 floats when MPU6050 absent, locks up CPU
        powerManager.begin(false);
    }
    systemStatus.setBootStage(BootStage::BS_WIFI);

    // ---- WIFI ----
    LOG_INFO("INIT", "Initializing WiFi...");
    wifiManager.begin();
    LOG_INFO("INIT", "WiFi init complete");
    
    if (wifiManager.isAPMode()) {
        systemStatus.markWarning("WiFi", "No credentials stored - AP mode active");
        LOG_INFO("INIT", "WiFi: WARNING - AP mode (no credentials)");
    } else if (wifiManager.isConnected()) {
        systemStatus.markOK("WiFi");
        LOG_INFO("INIT", "WiFi: OK - %s (%d dBm)", wifiManager.getSSID().c_str(), wifiManager.getRSSI());
    } else {
        systemStatus.markError("WiFi", "Connection failed");
        LOG_INFO("INIT", "WiFi: FAILED - Connection failed");
    }
    
    systemStatus.setBootStage(BootStage::BS_WEB_SERVER);
    
    // ---- WEB SERVER ----
    // In AP mode, configPortal handles web — skip main WebServer to avoid port 80 conflict
    if (!wifiManager.isAPMode()) {
        LOG_INFO("INIT", "Starting Web Server...");
        webServer.begin();
        webServer.setStateManager(&stateManager);
        webServer.setToolRepository(&toolRepo);
        webServer.setUserRepository(&userRepo);
        webServer.setLogRepository(&logRepo);
        webServer.setWeightService(&weightService);
        webServer.setWiFiManager(&wifiManager);
        webServer.setSystemStatus(&systemStatus);
        webServer.setAccessController(&accessController);
        webServer.setServerClient(&serverClient);
        systemStatus.markOK("WebServer");
        LOG_INFO("INIT", "WebServer: OK");
    } else {
        LOG_INFO("INIT", "WebServer: SKIPPED (AP mode - configPortal active)");
    }

    systemStatus.setBootStage(BootStage::BS_FINGERPRINT);

    // ---- FINGERPRINT ----
    initWithRetry("Fingerprint", [&](){ return fpDriver.begin(); },
        "No sensor on UART2 (RX=5, TX=4) - check wiring", 3, 2000);

    systemStatus.setBootStage(BootStage::BS_ACCESS_SERVER);

    // ---- DOOR SERVICE ----
    doorService.begin();
    systemStatus.markOK("Door");
    LOG_INFO("INIT", "Door: OK");

    // ---- SERVER CLIENT ----
    String serverUrl = storage.getString("cfg_server_url", "");
    String serverToken = storage.getString("cfg_server_token", "");
    if (serverUrl.length() > 0) {
        serverClient.begin(serverUrl.c_str(), serverToken.length() > 0 ? serverToken.c_str() : nullptr);
        systemStatus.markOK("ServerClient");
    } else {
        systemStatus.markWarning("ServerClient", "No server URL configured");
    }
    LOG_INFO("INIT", "ServerClient: %s",
             serverUrl.length() > 0 ? serverUrl.c_str() : "(not configured)");

    // ---- ACCESS CONTROLLER ----
    accessController.setFingerprintDriver(&fpDriver);
    accessController.setServerClient(&serverClient);
    accessController.setDoorService(&doorService);
    accessController.setUserRepository(&userRepo);
    accessController.setStorageManager(&storage);
    accessController.begin();
    systemStatus.markOK("AccessController");
    LOG_INFO("INIT", "AccessController: OK");

    // ---- DISPLAY MANAGER ----
    if (systemStatus.getStatus("Display") != ComponentStatus::ERROR) {
        LOG_INFO("INIT", "DisplayManager heap=%d", ESP.getFreeHeap());
        displayManager.init();
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

    // ---- SERIAL CLI ----
    cli.begin();
#endif

    // Print connection info
    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    if (wifiManager.isAPMode()) {
        LOG_INFO("INIT", "WiFi AP MODE ACTIVE");
        LOG_INFO("INIT", "SSID: %s", wifiManager.getSSID().c_str());
        LOG_INFO("INIT", "AP IP: %s", wifiManager.getIP().c_str());
        LOG_INFO("INIT", "Connect to configure WiFi credentials");
    } else {
        LOG_INFO("INIT", "WiFi Connected!");
        LOG_INFO("INIT", "IP Address: %s", wifiManager.getIP().c_str());
        LOG_INFO("INIT", "Signal: %d dBm", wifiManager.getRSSI());
    }

    // Print status summary
    LOG_INFO("INIT", "");
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "COMPONENT STATUS:");
    LOG_INFO("INIT", "-------------------------------------------");
    LOG_INFO("INIT", "Storage:     %s", systemStatus.getStatus("Storage") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "HX711:       %s", systemStatus.getStatus("HX711") == ComponentStatus::OK ? "[OK]" :
        systemStatus.getStatus("HX711") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "MPU6050:     %s", systemStatus.getStatus("MPU6050") == ComponentStatus::OK ? "[OK]" :
        systemStatus.getStatus("MPU6050") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "Display:     %s", systemStatus.getStatus("Display") == ComponentStatus::OK ? "[OK]" :
        systemStatus.getStatus("Display") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "WiFi:        %s", wifiManager.isConnected() ? "[OK]" :
        wifiManager.isAPMode() ? "[AP]" : "[FAIL]");
    LOG_INFO("INIT", "WebServer:   %s", systemStatus.getStatus("WebServer") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "Fingerprint: %s", systemStatus.getStatus("Fingerprint") == ComponentStatus::OK ? "[OK]" :
        systemStatus.getStatus("Fingerprint") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    LOG_INFO("INIT", "Door:        %s", systemStatus.getStatus("Door") == ComponentStatus::OK ? "[OK]" : "[---]");
    LOG_INFO("INIT", "Server:      %s", systemStatus.getStatus("ServerClient") == ComponentStatus::OK ? "[OK]" :
        systemStatus.getStatus("ServerClient") == ComponentStatus::WARNING ? "[WARN]" : "[---]");
    LOG_INFO("INIT", "AccessCtrl:  %s", systemStatus.getStatus("AccessController") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    LOG_INFO("INIT", "===========================================");

    if (systemStatus.hasErrors()) {
        LOG_INFO("INIT", "WARNING: Some components failed to initialize!");
        LOG_INFO("INIT", "Last error: %s", systemStatus.getLastError().c_str());
        LOG_INFO("INIT", "System will continue with reduced functionality");
        LOG_INFO("INIT", "===========================================");
    }

    // ---- CREATE TASKS ----
    LOG_INFO("INIT", "Creating tasks...");

    int tasksOk = 0, tasksFail = 0;
    #define CREATE_TASK(fn, name, stack, prio, handle, core) do { \
        if (xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, handle, core) != pdPASS) { \
            LOG_ERROR("INIT", "FAILED to create %s task", name); \
            tasksFail++; \
        } else { tasksOk++; } \
    } while(0)

    CREATE_TASK(stateTask, "State", 2560, PRIORITY_STATE, &stateTaskHandle, 0);
    CREATE_TASK(wifiTask, "WiFi", 6144, PRIORITY_WIFI, &wifiTaskHandle, 0);

    if (systemStatus.getStatus("Fingerprint") != ComponentStatus::ERROR) {
        CREATE_TASK(accessTask, "Access", 4096, PRIORITY_ACCESS, &accessTaskHandle, 0);
    }
    if (systemStatus.getStatus("HX711") != ComponentStatus::ERROR) {
        CREATE_TASK(weightTask, "Weight", 2560, PRIORITY_WEIGHT, &weightTaskHandle, 0);
    }
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        CREATE_TASK(motionTask, "Motion", 2560, PRIORITY_MOTION, &motionTaskHandle, 0);
    }
    if (systemStatus.getStatus("WebServer") == ComponentStatus::OK) {
        CREATE_TASK(webTask, "Web", 4096, PRIORITY_WEB, &webTaskHandle, 0);
    }
    if (systemStatus.getStatus("Display") != ComponentStatus::ERROR) {
        CREATE_TASK(displayTask, "Display", 2560, PRIORITY_DISPLAY, &displayTaskHandle, 1);
    }

    LOG_INFO("INIT", "Tasks created: %d OK, %d failed", tasksOk, tasksFail);

    systemStatus.setBootComplete();
    LOG_INFO("INIT", "Free heap: %d bytes", ESP.getFreeHeap());
    LOG_INFO("INIT", "===========================================");
    LOG_INFO("INIT", "System ready!");
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
    powerManager.update();
    systemStatus.update();

    // Heartbeat — proves loop() is running
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        LOG_INFO("SYS", "heartbeat heap=%d state=%d mode=%s",
                 ESP.getFreeHeap(), (int)powerManager.getCurrentState(),
                 (SystemStatus::getInstance().getOperationalMode() == OperationalMode::OP_AP_FULL) ? "AP" : "STA");
        lastHeartbeat = millis();
    }

    // Status LED (GPIO2, active LOW = ON)
    static unsigned long lastBlink = 0;
    bool connected = wifiManager.isConnected();
    bool hasErrors = systemStatus.hasErrors();

    if (apMode) {
        digitalWrite(PIN_LED, LOW); // solid ON = AP mode (highest priority)
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

    if (powerManager.getCurrentState() == PM_LIGHT_SLEEP) {
        unsigned long idle = millis() - powerManager.getWakeCount() * 60000;
        if (idle > 60000) {
            powerManager.enterDeepSleep();
        }
    }

    delay(10);
}

// FreeRTOS hooks: framework provides esp_vApplicationIdleHook / esp_vApplicationTickHook.
// Define here only if custom behavior needed (must use esp_ prefixed names).