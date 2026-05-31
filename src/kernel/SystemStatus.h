#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <Arduino.h>
#include <vector>
#include "../config/Config.h"

enum class BootStage {
    BS_STORAGE,
    BS_HX711,
    BS_MPU6050,
    BS_DISPLAY,
    BS_WIFI,
    BS_WEB_SERVER,
    BS_FINGERPRINT,
    BS_ACCESS_SERVER,
    BS_COMPLETE
};

enum class ComponentStatus {
    UNKNOWN,
    OK,
    WARNING,
    ERROR
};

struct ComponentInfo {
    String name;
    ComponentStatus status;
    String lastError;
    unsigned long errorTime;
    int errorCount;
    
    ComponentInfo() : status(ComponentStatus::UNKNOWN), errorTime(0), errorCount(0) {}
};

class SystemStatus {
public:
    static SystemStatus& getInstance();
    
    void begin();
    void update();
    
    // Component registration
    void markOK(const char* component);
    void markWarning(const char* component, const char* error);
    void markError(const char* component, const char* error);
    
    // Status queries
    ComponentStatus getStatus(const char* component);
    ComponentStatus getOverallStatus();
    String getLastError();
    int getErrorCount();
    unsigned long getUptime();
    bool hasErrors();
    
    // Component list
    std::vector<ComponentInfo> getAllComponents();
    int getOKCount();
    int getWarningCount();
    int getErrorComponentCount();
    
    // Boot stage
    void setBootStage(BootStage stage);
    BootStage getBootStage();
    void setBootComplete();
    bool isBootComplete();
    
    // Operational mode (AP full-speed vs STA power-save)
    void setOperationalMode(OperationalMode mode);
    OperationalMode getOperationalMode();

    // Error recovery
    void clearErrors();
    void resetComponent(const char* component);

private:
    SystemStatus() : bootComplete(false), bootStart(0), currentStage(BootStage::BS_STORAGE),
                     currentOpMode(OperationalMode::OP_AP_FULL) {}
    
    std::vector<ComponentInfo> components;
    String lastError;
    int totalErrorCount;
    bool bootComplete;
    unsigned long bootStart;
    BootStage currentStage;
    OperationalMode currentOpMode;
    
    ComponentInfo* findComponent(const char* name);
    void ensureComponent(const char* name);
};

#endif