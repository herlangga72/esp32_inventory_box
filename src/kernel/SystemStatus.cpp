#include "SystemStatus.h"
#include "../utils/LogManager.h"
#include <cstring>

static ComponentInfoFixed* findComponent(SystemStatusMemory* mem, const char* name) {
    for (int i = 0; i < mem->componentCount; i++) {
        if (strcmp(mem->components[i].name, name) == 0) {
            return &mem->components[i];
        }
    }
    return nullptr;
}

static ComponentInfoFixed* ensureComponent(SystemStatusMemory* mem, const char* name) {
    ComponentInfoFixed* existing = findComponent(mem, name);
    if (existing) return existing;

    if (mem->componentCount >= MAX_COMPONENTS) return nullptr;

    ComponentInfoFixed* comp = &mem->components[mem->componentCount++];
    memset(comp, 0, sizeof(ComponentInfoFixed));
    strncpy(comp->name, name, sizeof(comp->name) - 1);
    comp->status = ComponentStatus::UNKNOWN;
    return comp;
}

void ss_begin(SystemStatusMemory* mem) {
    memset(mem, 0, sizeof(SystemStatusMemory));
    mem->bootStartMs = millis();
    mem->currentBootStage = static_cast<uint8_t>(BootStage::BS_STORAGE);
    mem->currentOpMode = static_cast<uint8_t>(OperationalMode::OP_AP_FULL);

    // Pre-register all components
    const char* names[] = {"Storage", "HX711", "MPU6050", "Display", "WiFi",
                           "WebServer", "Fingerprint", "Door", "ServerClient",
                           "AccessController", "RTC"};
    for (int i = 0; i < 11; i++) {
        ensureComponent(mem, names[i]);
    }

    LOG_INFO("STATUS", "SystemStatus initialized");
}

void ss_update(SystemStatusMemory* mem) {}

void ss_markOK(SystemStatusMemory* mem, const char* component) {
    ComponentInfoFixed* comp = ensureComponent(mem, component);
    if (comp) {
        comp->status = ComponentStatus::OK;
        LOG_INFO("STATUS", "%s: OK", component);
    }
}

void ss_markWarning(SystemStatusMemory* mem, const char* component, const char* error) {
    ComponentInfoFixed* comp = ensureComponent(mem, component);
    if (comp) {
        comp->status = ComponentStatus::WARNING;
        strncpy(comp->lastError, error, sizeof(comp->lastError) - 1);
        comp->errorTime = millis();
        comp->errorCount++;
        snprintf(mem->lastErrorMsg, sizeof(mem->lastErrorMsg), "%s: %s", component, error);
        mem->totalErrorCount++;
        LOG_INFO("STATUS", "%s: WARNING - %s", component, error);
    }
}

void ss_markError(SystemStatusMemory* mem, const char* component, const char* error) {
    ComponentInfoFixed* comp = ensureComponent(mem, component);
    if (comp) {
        comp->status = ComponentStatus::ERROR;
        strncpy(comp->lastError, error, sizeof(comp->lastError) - 1);
        comp->errorTime = millis();
        comp->errorCount++;
        snprintf(mem->lastErrorMsg, sizeof(mem->lastErrorMsg), "%s: %s", component, error);
        mem->totalErrorCount++;
        LOG_INFO("STATUS", "%s: ERROR - %s", component, error);
    }
}

ComponentStatus ss_getStatus(const SystemStatusMemory* mem, const char* component) {
    for (int i = 0; i < mem->componentCount; i++) {
        if (strcmp(mem->components[i].name, component) == 0) {
            return mem->components[i].status;
        }
    }
    return ComponentStatus::UNKNOWN;
}

ComponentStatus ss_getOverallStatus(const SystemStatusMemory* mem) {
    bool hasError = false, hasWarning = false;
    for (int i = 0; i < mem->componentCount; i++) {
        if (mem->components[i].status == ComponentStatus::ERROR) hasError = true;
        if (mem->components[i].status == ComponentStatus::WARNING) hasWarning = true;
    }
    if (hasError) return ComponentStatus::ERROR;
    if (hasWarning) return ComponentStatus::WARNING;
    for (int i = 0; i < mem->componentCount; i++) {
        if (mem->components[i].status == ComponentStatus::UNKNOWN) return ComponentStatus::WARNING;
    }
    return ComponentStatus::OK;
}

const char* ss_getLastError(const SystemStatusMemory* mem) { return mem->lastErrorMsg; }
int ss_getErrorCount(const SystemStatusMemory* mem) { return mem->totalErrorCount; }
unsigned long ss_getUptime(const SystemStatusMemory* mem) {
    return mem->bootComplete ? (millis() - mem->bootStartMs) : 0;
}
bool ss_hasErrors(const SystemStatusMemory* mem) {
    for (int i = 0; i < mem->componentCount; i++) {
        if (mem->components[i].status == ComponentStatus::ERROR ||
            mem->components[i].status == ComponentStatus::WARNING) return true;
    }
    return false;
}

int ss_getOKCount(const SystemStatusMemory* mem) {
    int c = 0;
    for (int i = 0; i < mem->componentCount; i++)
        if (mem->components[i].status == ComponentStatus::OK) c++;
    return c;
}

int ss_getWarningCount(const SystemStatusMemory* mem) {
    int c = 0;
    for (int i = 0; i < mem->componentCount; i++)
        if (mem->components[i].status == ComponentStatus::WARNING) c++;
    return c;
}

int ss_getErrorComponentCount(const SystemStatusMemory* mem) {
    int c = 0;
    for (int i = 0; i < mem->componentCount; i++)
        if (mem->components[i].status == ComponentStatus::ERROR) c++;
    return c;
}

void ss_setBootStage(SystemStatusMemory* mem, BootStage stage) {
    mem->currentBootStage = static_cast<uint8_t>(stage);
}

BootStage ss_getBootStage(const SystemStatusMemory* mem) {
    return static_cast<BootStage>(mem->currentBootStage);
}
void ss_setBootComplete(SystemStatusMemory* mem) { mem->bootComplete = true; }
bool ss_isBootComplete(const SystemStatusMemory* mem) { return mem->bootComplete; }

void ss_setOperationalMode(SystemStatusMemory* mem, OperationalMode mode) {
    mem->currentOpMode = static_cast<uint8_t>(mode);
}

OperationalMode ss_getOperationalMode(const SystemStatusMemory* mem) {
    return static_cast<OperationalMode>(mem->currentOpMode);
}

void ss_clearErrors(SystemStatusMemory* mem) {
    for (int i = 0; i < mem->componentCount; i++) {
        if (mem->components[i].status == ComponentStatus::ERROR ||
            mem->components[i].status == ComponentStatus::WARNING) {
            mem->components[i].status = ComponentStatus::OK;
            mem->components[i].lastError[0] = '\0';
            mem->components[i].errorCount = 0;
        }
    }
    mem->lastErrorMsg[0] = '\0';
}

void ss_resetComponent(SystemStatusMemory* mem, const char* component) {
    ComponentInfoFixed* comp = findComponent(mem, component);
    if (comp) {
        comp->status = ComponentStatus::UNKNOWN;
        comp->lastError[0] = '\0';
    }
}
