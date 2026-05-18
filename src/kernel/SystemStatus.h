#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <Arduino.h>
#include <vector>

enum class BootStage {
    STORAGE,
    HX711,
    MPU6050,
    DISPLAY,
    WIFI,
    WEB_SERVER,
    COMPLETE
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
    
    // Error recovery
    void clearErrors();
    void resetComponent(const char* component);

private:
    SystemStatus() : bootComplete(false), bootStart(0), currentStage(BootStage::STORAGE) {}
    
    std::vector<ComponentInfo> components;
    String lastError;
    int totalErrorCount;
    bool bootComplete;
    unsigned long bootStart;
    BootStage currentStage;
    
    ComponentInfo* findComponent(const char* name);
    void ensureComponent(const char* name);
};

#endif