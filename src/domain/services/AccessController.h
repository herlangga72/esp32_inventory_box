#ifndef ACCESS_CONTROLLER_H
#define ACCESS_CONTROLLER_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"

class FingerprintDriver;
class ServerClient;

enum class AccessState : uint8_t {
    IDLE,
    SCANNING,
    CHECKING_SERVER,
    LOCAL_AUTH_CHECK,
    GRANTED,
    DENIED,
    UNLOCKING,
    UNLOCKED,
    LOCKING,
    ENROLLING
};

// ---- Lifecycle ----
void ac_init(AccessControllerMemory* mem);
void ac_dispatchMessage(AccessControllerMemory* mem, const ServiceMessage& msg,
                        FingerprintDriver* fp, ServerClient* sc, DoorServiceMemory* ds);
void ac_update(AccessControllerMemory* mem, FingerprintDriver* fp,
               ServerClient* sc, DoorServiceMemory* ds);

// ---- Status queries (used by WebServer) ----
int ac_getState(const AccessControllerMemory* mem);
const char* ac_getStateName(const AccessControllerMemory* mem);
int ac_getLastFpId(const AccessControllerMemory* mem);
const char* ac_getLastEvent(const AccessControllerMemory* mem);
bool ac_isEnrolling(const AccessControllerMemory* mem);
int ac_getEnrollStep(const AccessControllerMemory* mem);
int ac_getEnrollingFpId(const AccessControllerMemory* mem);
bool ac_isLocalFallbackEnabled(const AccessControllerMemory* mem);
int ac_getServerStatus(const AccessControllerMemory* mem, ServerClient* sc);
unsigned long ac_getServerFailDuration(const AccessControllerMemory* mem, ServerClient* sc);
unsigned long ac_getLastServerResponseTime(const AccessControllerMemory* mem, ServerClient* sc);

// ---- Commands used directly by WebServer (return bool for sync response) ----
bool ac_beginEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId);
bool ac_cancelEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp);
bool ac_deleteFingerprint(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId);
bool ac_remoteUnlock(AccessControllerMemory* mem, DoorServiceMemory* ds);

#endif // ACCESS_CONTROLLER_H
