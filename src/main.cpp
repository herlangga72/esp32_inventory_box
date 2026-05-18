#include <Arduino.h>
#include <WiFi.h>

#include "config/Config.h"
#include "hal/HX711Driver.h"
#include "hal/MPU6050Driver.h"
#include "hal/SSD1306Driver.h"
#include "hal/InterruptManager.h"

#include "data/StorageManager.h"
#include "data/ToolRepository.h"
#include "data/UserRepository.h"
#include "data/LogRepository.h"

#include "domain/events/EventBus.h"
#include "domain/services/WeightService.h"
#include "domain/services/MotionService.h"
#include "domain/services/StateManager.h"

#include "presentation/WebServer.h"
#include "kernel/PowerManager.h"
#include "kernel/WiFiManager.h"
#include "kernel/SystemStatus.h"
#include "utils/Logger.h"

// Task handles
TaskHandle_t weightTaskHandle = NULL;
TaskHandle_t motionTaskHandle = NULL;
TaskHandle_t stateTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;

// Global instances
StorageManager storage;
HX711Driver hx711(PIN_HX711_DT, PIN_HX711_SCK, PIN_HX711_DRDY);
MPU6050Driver mpu;
SSD1306Driver display(PIN_DISPLAY_SDA, PIN_DISPLAY_SCL, PIN_DISPLAY_RST);

ToolRepository toolRepo(&storage);
UserRepository userRepo(&storage);
LogRepository logRepo(&storage);

EventBus* eventBus = EventBus::getInstance();
WeightService weightService(&hx711);
MotionService motionService(&mpu);
StateManager stateManager(eventBus);

WebServerManager webServer(eventBus);
PowerManager powerManager;
WiFiManager wifiManager;
SystemStatus& systemStatus = SystemStatus::getInstance();

// Task priorities
const int PRIORITY_WEIGHT = 8;
const int PRIORITY_MOTION = 8;
const int PRIORITY_STATE = 10;
const int PRIORITY_WEB = 5;
const int PRIORITY_DISPLAY = 3;
const int PRIORITY_WIFI = 6;

// ============= INIT HELPERS =============

void initWithStatus(const char* name, bool (*initFn)(), void (*successFn)(), const char* errorMsg) {
    Serial.printf("[Init] Starting %s...\n", name);
    bool result = initFn();
    if (result) {
        successFn();
    } else {
        systemStatus.markError(name, errorMsg);
        Serial.printf("[Init] %s FAILED: %s\n", name, errorMsg);
    }
}

// ============= TASKS =============

void weightTask(void* param) {
    // Check if HX711 is working
    if (systemStatus.getStatus("HX711") == ComponentStatus::ERROR) {
        Serial.println("[Weight] HX711 not available, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    TickType_t lastWake = xTaskGetTickCount();
    
    while (true) {
        // Process weight data
        if (InterruptManager::isHX711Ready()) {
            int32_t raw = hx711.readRaw();
            if (raw != INT32_MIN) {
                weightService.onRawReading(raw);
            }
        }
        
        // Also poll periodically
        weightService.update();
        
        // Notify power manager
        powerManager.onActivity();
        
        // 10Hz rate
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
    }
}

void motionTask(void* param) {
    // Check if MPU6050 is working
    if (systemStatus.getStatus("MPU6050") == ComponentStatus::ERROR) {
        Serial.println("[Motion] MPU6050 not available, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        // Wait for motion interrupt
        if (InterruptManager::isMPUTriggered()) {
            motionService.update();
            
            // Check for significant motion
            MotionType motion = motionService.getCurrentMotion();
            stateManager.onMotionDetected(motion);
            
            // Handle wake from deep sleep
            if (powerManager.getCurrentState() == PowerState::POWER_DEEP_SLEEP) {
                powerManager.handleWakeFromMotion();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void stateTask(void* param) {
    while (true) {
        stateManager.update();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void webTask(void* param) {
    // Check if WebServer is working
    if (systemStatus.getStatus("WebServer") == ComponentStatus::ERROR) {
        Serial.println("[Web] WebServer not available, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        webServer.handle();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void displayTask(void* param) {
    // Check if Display is working
    if (systemStatus.getStatus("Display") == ComponentStatus::ERROR) {
        Serial.println("[Display] Display not available, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        // Update display (1Hz) - placeholder for DisplayManager
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void wifiTask(void* param) {
    while (true) {
        wifiManager.update();
        
        // Also check web server in AP mode
        if (wifiManager.isAPMode()) {
            webServer.handle();
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============= SETUP =============

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println();
    Serial.println("===========================================");
    Serial.println("ESP32 Inventory Box Starting...");
    Serial.println("===========================================");
    
    // Initialize system status
    systemStatus.begin();
    systemStatus.setBootStage(BootStage::STORAGE);
    
    // ---- STORAGE ----
    Serial.println("[Init] Initializing Storage...");
    if (storage.begin()) {
        systemStatus.markOK("Storage");
        Serial.println("[Init] Storage: OK");
    } else {
        systemStatus.markError("Storage", "NVS initialization failed");
        Serial.println("[Init] Storage: FAILED - NVS initialization failed");
    }
    
    systemStatus.setBootStage(BootStage::HX711);
    
    // ---- HX711 ----
    Serial.println("[Init] Initializing HX711...");
    hx711.begin();
    delay(100);
    
    // Check if HX711 responds
    int32_t testRead = hx711.readRaw();
    if (testRead != INT32_MIN && testRead != 0) {
        systemStatus.markOK("HX711");
        Serial.println("[Init] HX711: OK");
    } else {
        systemStatus.markError("HX711", "No response - check wiring (DT=16, SCK=17)");
        Serial.println("[Init] HX711: FAILED - No response");
    }
    
    systemStatus.setBootStage(BootStage::MPU6050);
    
    // ---- MPU6050 ----
    Serial.println("[Init] Initializing MPU6050...");
    bool mpuOK = mpu.begin();
    if (mpuOK) {
        systemStatus.markOK("MPU6050");
        Serial.println("[Init] MPU6050: OK");
    } else {
        systemStatus.markError("MPU6050", "I2C address 0x68 not found - check wiring");
        Serial.println("[Init] MPU6050: FAILED - I2C address not found");
    }
    
    systemStatus.setBootStage(BootStage::DISPLAY);
    
    // ---- DISPLAY ----
    Serial.println("[Init] Initializing Display...");
    display.init();
    delay(100);
    
    // Check if display initialized (basic check)
    if (display.isInitialized()) {
        systemStatus.markOK("Display");
        Serial.println("[Init] Display: OK");
    } else {
        systemStatus.markError("Display", "I2C address 0x3C not found - check wiring");
        Serial.println("[Init] Display: FAILED - I2C address not found");
    }
    
    // ---- INTERRUPTS ----
    Serial.println("[Init] Configuring interrupts...");
    InterruptManager::begin();
    Serial.println("[Init] Interrupts: OK");
    
    // ---- SERVICES ----
    Serial.println("[Init] Initializing services...");
    weightService.begin();
    motionService.begin();
    
    // Load saved baseline
    float savedBaseline = storage.getFloat("baseline", 0.0f);
    if (savedBaseline > 0) {
        weightService.setBaseline(savedBaseline);
        stateManager.setBaseline(savedBaseline);
        Serial.printf("[Init] Loaded baseline: %.1f g\n", savedBaseline);
    }
    
    // Calibrate motion baseline (don't fail if MPU6050 is down)
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        motionService.calibrateBaseline();
    }
    
    // Initialize managers
    stateManager.begin();
    stateManager.setWeightService(&weightService);
    stateManager.setMotionService(&motionService);
    stateManager.setToolRepository(&toolRepo);
    stateManager.setLogRepository(&logRepo);
    
    // Initialize power manager
    powerManager.begin();
    powerManager.setBaseline(savedBaseline);
    
    systemStatus.setBootStage(BootStage::WIFI);
    
    // ---- WIFI ----
    Serial.println("[Init] Initializing WiFi...");
    wifiManager.begin();
    
    if (wifiManager.isAPMode()) {
        systemStatus.markWarning("WiFi", "No credentials stored - AP mode active");
        Serial.println("[Init] WiFi: WARNING - AP mode (no credentials)");
    } else if (wifiManager.isConnected()) {
        systemStatus.markOK("WiFi");
        Serial.printf("[Init] WiFi: OK - %s (%d dBm)\n", 
            wifiManager.getSSID().c_str(), wifiManager.getRSSI());
    } else {
        systemStatus.markError("WiFi", "Connection failed");
        Serial.println("[Init] WiFi: FAILED - Connection failed");
    }
    
    systemStatus.setBootStage(BootStage::WEB_SERVER);
    
    // ---- WEB SERVER ----
    Serial.println("[Init] Starting Web Server...");
    webServer.begin();
    webServer.setStateManager(&stateManager);
    webServer.setToolRepository(&toolRepo);
    webServer.setUserRepository(&userRepo);
    webServer.setLogRepository(&logRepo);
    webServer.setWeightService(&weightService);
    webServer.setWiFiManager(&wifiManager);
    webServer.setSystemStatus(&systemStatus);
    
    systemStatus.markOK("WebServer");
    Serial.println("[Init] WebServer: OK");
    
    // Print connection info
    Serial.println();
    Serial.println("===========================================");
    if (wifiManager.isAPMode()) {
        Serial.println("WiFi AP MODE ACTIVE");
        Serial.printf("SSID: %s\n", wifiManager.getSSID().c_str());
        Serial.printf("AP IP: %s\n", wifiManager.getIP().c_str());
        Serial.println("Connect to configure WiFi credentials");
    } else {
        Serial.println("WiFi Connected!");
        Serial.printf("IP Address: %s\n", wifiManager.getIP().c_str());
        Serial.printf("Signal: %d dBm\n", wifiManager.getRSSI());
    }
    
    // Print status summary
    Serial.println();
    Serial.println("===========================================");
    Serial.println("COMPONENT STATUS:");
    Serial.println("-------------------------------------------");
    Serial.printf("Storage:     %s\n", 
        systemStatus.getStatus("Storage") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    Serial.printf("HX711:       %s\n", 
        systemStatus.getStatus("HX711") == ComponentStatus::OK ? "[OK]" : 
        systemStatus.getStatus("HX711") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    Serial.printf("MPU6050:     %s\n", 
        systemStatus.getStatus("MPU6050") == ComponentStatus::OK ? "[OK]" : 
        systemStatus.getStatus("MPU6050") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    Serial.printf("Display:     %s\n", 
        systemStatus.getStatus("Display") == ComponentStatus::OK ? "[OK]" : 
        systemStatus.getStatus("Display") == ComponentStatus::ERROR ? "[FAIL]" : "[---]");
    Serial.printf("WiFi:        %s\n", 
        wifiManager.isConnected() ? "[OK]" : 
        wifiManager.isAPMode() ? "[AP]" : "[FAIL]");
    Serial.printf("WebServer:   %s\n", 
        systemStatus.getStatus("WebServer") == ComponentStatus::OK ? "[OK]" : "[FAIL]");
    Serial.println("===========================================");
    
    if (systemStatus.hasErrors()) {
        Serial.println("WARNING: Some components failed to initialize!");
        Serial.printf("Last error: %s\n", systemStatus.getLastError().c_str());
        Serial.println("System will continue with reduced functionality");
        Serial.println("===========================================");
    }
    
    // ---- CREATE TASKS ----
    Serial.println("[Init] Creating tasks...");
    
    // Only create tasks for working components
    xTaskCreatePinnedToCore(stateTask, "State", 2048, NULL, PRIORITY_STATE, &stateTaskHandle, 0);
    xTaskCreatePinnedToCore(wifiTask, "WiFi", 2048, NULL, PRIORITY_WIFI, &wifiTaskHandle, 0);
    
    // Conditional task creation
    if (systemStatus.getStatus("HX711") != ComponentStatus::ERROR) {
        xTaskCreatePinnedToCore(weightTask, "Weight", 2048, NULL, PRIORITY_WEIGHT, &weightTaskHandle, 0);
    }
    
    if (systemStatus.getStatus("MPU6050") != ComponentStatus::ERROR) {
        xTaskCreatePinnedToCore(motionTask, "Motion", 2048, NULL, PRIORITY_MOTION, &motionTaskHandle, 0);
    }
    
    if (systemStatus.getStatus("WebServer") != ComponentStatus::ERROR) {
        xTaskCreatePinnedToCore(webTask, "Web", 4096, NULL, PRIORITY_WEB, &webTaskHandle, 0);
    }
    
    if (systemStatus.getStatus("Display") != ComponentStatus::ERROR) {
        xTaskCreatePinnedToCore(displayTask, "Display", 1024, NULL, PRIORITY_DISPLAY, &displayTaskHandle, 1);
    }
    
    Serial.println("[Init] Tasks created");
    
    systemStatus.setBootComplete();
    Serial.printf("[Init] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("===========================================");
    Serial.println("System ready!");
    Serial.println("===========================================");
}

// ============= MAIN LOOP =============

void loop() {
    // Update power manager (handles sleep states)
    powerManager.update();
    
    // Update system status
    systemStatus.update();
    
    // Check for deep sleep trigger
    if (powerManager.getCurrentState() == PowerState::POWER_LIGHT_SLEEP) {
        unsigned long idle = millis() - powerManager.getWakeCount() * 60000;
        if (idle > 60000) {
            powerManager.enterDeepSleep();
        }
    }
    
    delay(10);
}

// ============= IDLE HOOK =============

void vApplicationIdleHook(void) {
    // Tickless idle - ESP32 will enter light sleep automatically
}

// ============= TICK HOOK =============

void vApplicationTickHook(void) {
    // Can be used for periodic tasks
}