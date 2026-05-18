#include "SystemStatus.h"

SystemStatus& SystemStatus::getInstance() {
    static SystemStatus instance;
    return instance;
}

void SystemStatus::begin() {
    bootStart = millis();
    totalErrorCount = 0;
    bootComplete = false;
    
    // Register all components
    components.clear();
    
    ComponentInfo storage;
    storage.name = "Storage";
    components.push_back(storage);
    
    ComponentInfo hx711;
    hx711.name = "HX711";
    components.push_back(hx711);
    
    ComponentInfo mpu;
    mpu.name = "MPU6050";
    components.push_back(mpu);
    
    ComponentInfo display;
    display.name = "Display";
    components.push_back(display);
    
    ComponentInfo wifi;
    wifi.name = "WiFi";
    components.push_back(wifi);
    
    ComponentInfo webserver;
    webserver.name = "WebServer";
    components.push_back(webserver);
    
    Serial.println("[Status] SystemStatus initialized");
}

void SystemStatus::update() {
    // Could add periodic health checks here
}

ComponentInfo* SystemStatus::findComponent(const char* name) {
    for (auto& comp : components) {
        if (comp.name == name) {
            return &comp;
        }
    }
    return nullptr;
}

void SystemStatus::ensureComponent(const char* name) {
    if (!findComponent(name)) {
        ComponentInfo comp;
        comp.name = name;
        components.push_back(comp);
    }
}

void SystemStatus::markOK(const char* component) {
    ComponentInfo* comp = findComponent(component);
    if (!comp) {
        ensureComponent(component);
        comp = findComponent(component);
    }
    
    if (comp) {
        comp->status = ComponentStatus::OK;
        Serial.printf("[Status] %s: OK\n", component);
    }
}

void SystemStatus::markWarning(const char* component, const char* error) {
    ComponentInfo* comp = findComponent(component);
    if (!comp) {
        ensureComponent(component);
        comp = findComponent(component);
    }
    
    if (comp) {
        comp->status = ComponentStatus::WARNING;
        comp->lastError = error;
        comp->errorTime = millis();
        comp->errorCount++;
        lastError = String(component) + ": " + error;
        totalErrorCount++;
        
        Serial.printf("[Status] %s: WARNING - %s\n", component, error);
    }
}

void SystemStatus::markError(const char* component, const char* error) {
    ComponentInfo* comp = findComponent(component);
    if (!comp) {
        ensureComponent(component);
        comp = findComponent(component);
    }
    
    if (comp) {
        comp->status = ComponentStatus::ERROR;
        comp->lastError = error;
        comp->errorTime = millis();
        comp->errorCount++;
        lastError = String(component) + ": " + error;
        totalErrorCount++;
        
        Serial.printf("[Status] %s: ERROR - %s\n", component, error);
    }
}

ComponentStatus SystemStatus::getStatus(const char* component) {
    ComponentInfo* comp = findComponent(component);
    return comp ? comp->status : ComponentStatus::UNKNOWN;
}

ComponentStatus SystemStatus::getOverallStatus() {
    bool hasError = false;
    bool hasWarning = false;
    
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::ERROR) hasError = true;
        if (comp.status == ComponentStatus::WARNING) hasWarning = true;
    }
    
    if (hasError) return ComponentStatus::ERROR;
    if (hasWarning) return ComponentStatus::WARNING;
    
    // Check if all known components are OK
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::UNKNOWN) {
            return ComponentStatus::WARNING;
        }
    }
    
    return ComponentStatus::OK;
}

String SystemStatus::getLastError() {
    return lastError;
}

int SystemStatus::getErrorCount() {
    return totalErrorCount;
}

unsigned long SystemStatus::getUptime() {
    return bootComplete ? (millis() - bootStart) : 0;
}

bool SystemStatus::hasErrors() {
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::ERROR || 
            comp.status == ComponentStatus::WARNING) {
            return true;
        }
    }
    return false;
}

std::vector<ComponentInfo> SystemStatus::getAllComponents() {
    return components;
}

int SystemStatus::getOKCount() {
    int count = 0;
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::OK) count++;
    }
    return count;
}

int SystemStatus::getWarningCount() {
    int count = 0;
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::WARNING) count++;
    }
    return count;
}

int SystemStatus::getErrorComponentCount() {
    int count = 0;
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::ERROR) count++;
    }
    return count;
}

void SystemStatus::setBootStage(BootStage stage) {
    currentStage = stage;
    Serial.printf("[Status] Boot stage: %d\n", (int)stage);
}

BootStage SystemStatus::getBootStage() {
    return currentStage;
}

void SystemStatus::setBootComplete() {
    bootComplete = true;
    Serial.println("[Status] Boot complete");
}

bool SystemStatus::isBootComplete() {
    return bootComplete;
}

void SystemStatus::clearErrors() {
    for (auto& comp : components) {
        if (comp.status == ComponentStatus::ERROR || 
            comp.status == ComponentStatus::WARNING) {
            comp.status = ComponentStatus::OK;
            comp.lastError = "";
            comp.errorCount = 0;
        }
    }
    lastError = "";
    Serial.println("[Status] Errors cleared");
}

void SystemStatus::resetComponent(const char* component) {
    ComponentInfo* comp = findComponent(component);
    if (comp) {
        comp->status = ComponentStatus::UNKNOWN;
        comp->lastError = "";
        Serial.printf("[Status] %s reset\n", component);
    }
}