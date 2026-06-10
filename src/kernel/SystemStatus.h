#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <Arduino.h>
#include "../config/Config.h"
#include "ServiceRegistry.h"

enum class BootStage {
    BS_STORAGE, BS_HX711, BS_MPU6050, BS_DISPLAY,
    BS_WIFI, BS_WEB_SERVER, BS_FINGERPRINT, BS_ACCESS_SERVER,
    BS_COMPLETE
};

// All functions operate on registry's SystemStatusMemory*
void ss_begin(SystemStatusMemory* mem);
void ss_update(SystemStatusMemory* mem);

void ss_markOK(SystemStatusMemory* mem, const char* component);
void ss_markWarning(SystemStatusMemory* mem, const char* component, const char* error);
void ss_markError(SystemStatusMemory* mem, const char* component, const char* error);

ComponentStatus ss_getStatus(const SystemStatusMemory* mem, const char* component);
ComponentStatus ss_getOverallStatus(const SystemStatusMemory* mem);
const char*     ss_getLastError(const SystemStatusMemory* mem);
int             ss_getErrorCount(const SystemStatusMemory* mem);
unsigned long   ss_getUptime(const SystemStatusMemory* mem);
bool            ss_hasErrors(const SystemStatusMemory* mem);

int  ss_getOKCount(const SystemStatusMemory* mem);
int  ss_getWarningCount(const SystemStatusMemory* mem);
int  ss_getErrorComponentCount(const SystemStatusMemory* mem);

void ss_setBootStage(SystemStatusMemory* mem, BootStage stage);
BootStage ss_getBootStage(const SystemStatusMemory* mem);
void ss_setBootComplete(SystemStatusMemory* mem);
bool ss_isBootComplete(const SystemStatusMemory* mem);

void ss_setOperationalMode(SystemStatusMemory* mem, OperationalMode mode);
OperationalMode ss_getOperationalMode(const SystemStatusMemory* mem);

void ss_clearErrors(SystemStatusMemory* mem);
void ss_resetComponent(SystemStatusMemory* mem, const char* component);

// Inline wrappers that auto-resolve from g_registry
inline SystemStatusMemory* _ss() { return g_registry.getSystemStatus(); }
inline void systemStatus_begin() { ss_begin(_ss()); }
inline void systemStatus_update() { ss_update(_ss()); }
inline void systemStatus_markOK(const char* c) { ss_markOK(_ss(), c); }
inline void systemStatus_markWarning(const char* c, const char* e) { ss_markWarning(_ss(), c, e); }
inline void systemStatus_markError(const char* c, const char* e) { ss_markError(_ss(), c, e); }
inline ComponentStatus systemStatus_getStatus(const char* c) { return ss_getStatus(_ss(), c); }
inline ComponentStatus systemStatus_getOverallStatus() { return ss_getOverallStatus(_ss()); }
inline const char* systemStatus_getLastError() { return ss_getLastError(_ss()); }
inline int systemStatus_getErrorCount() { return ss_getErrorCount(_ss()); }
inline unsigned long systemStatus_getUptime() { return ss_getUptime(_ss()); }
inline bool systemStatus_hasErrors() { return ss_hasErrors(_ss()); }
inline int systemStatus_getOKCount() { return ss_getOKCount(_ss()); }
inline int systemStatus_getWarningCount() { return ss_getWarningCount(_ss()); }
inline int systemStatus_getErrorComponentCount() { return ss_getErrorComponentCount(_ss()); }
inline void systemStatus_setBootStage(BootStage s) { ss_setBootStage(_ss(), s); }
inline BootStage systemStatus_getBootStage() { return ss_getBootStage(_ss()); }
inline void systemStatus_setBootComplete() { ss_setBootComplete(_ss()); }
inline bool systemStatus_isBootComplete() { return ss_isBootComplete(_ss()); }
inline void systemStatus_setOperationalMode(OperationalMode m) { ss_setOperationalMode(_ss(), m); }
inline OperationalMode systemStatus_getOperationalMode() { return ss_getOperationalMode(_ss()); }
inline void systemStatus_clearErrors() { ss_clearErrors(_ss()); }
inline void systemStatus_resetComponent(const char* c) { ss_resetComponent(_ss(), c); }

#endif // SYSTEM_STATUS_H
