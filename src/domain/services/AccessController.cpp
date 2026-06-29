#include "AccessController.h"
#include "../../hal/FingerprintDriver.h"
#include "../../kernel/ServerClient.h"
#include "DoorService.h"
#include "../../data/UserRepository.h"
#include "../../data/StorageManager.h"
#include "../../config/Config.h"
#include "../../utils/LogManager.h"
#include "../events/EventBus.h"

extern StorageManager storage;

// ---- State constants (match AccessState enum) ----
enum : uint8_t {
    AC_IDLE, AC_SCANNING, AC_CHECKING_SERVER, AC_LOCAL_AUTH_CHECK,
    AC_GRANTED, AC_DENIED, AC_UNLOCKING, AC_UNLOCKED, AC_LOCKING, AC_ENROLLING
};

static const unsigned long DENIED_DISPLAY_TIME_MS = 2000;

// ---- Internal helpers (forward declarations) ----
static void ac_transition(AccessControllerMemory* mem, uint8_t newState);
static void ac_logAccess(AccessControllerMemory* mem, const char* decision,
                         int fpId, int userId, const char* userName, const char* reason);
static void ac_publishEvent(DomainEvent type, int fpId, int userId,
                             const char* userName, const char* reason);

// State handlers
static void ac_handleIdle(AccessControllerMemory* mem, FingerprintDriver* fp);
static void ac_handleScanning(AccessControllerMemory* mem, FingerprintDriver* fp, ServerClient* sc);
static void ac_handleServerCheck(AccessControllerMemory* mem, ServerClient* sc);
static void ac_handleLocalAuthCheck(AccessControllerMemory* mem);
static void ac_handleGranted(AccessControllerMemory* mem, DoorServiceMemory* ds);
static void ac_handleDenied(AccessControllerMemory* mem);
static void ac_handleUnlocking(AccessControllerMemory* mem, DoorServiceMemory* ds);
static void ac_handleUnlocked(AccessControllerMemory* mem, DoorServiceMemory* ds);
static void ac_handleLocking(AccessControllerMemory* mem, DoorServiceMemory* ds);
static void ac_handleEnrolling(AccessControllerMemory* mem, FingerprintDriver* fp);

// ======================================================================
// LIFECYCLE
// ======================================================================

void ac_init(AccessControllerMemory* mem) {
    memset(mem, 0, sizeof(AccessControllerMemory));
    mem->state               = AC_IDLE;
    mem->stateStartMs         = millis();
    mem->lastFpId             = -1;
    mem->enrollingFpId        = -1;
    mem->enrollStep           = -1;
    mem->localFallbackEnabled = 1;

    // Load config from NVS (heap-free)
    char fallbackBuf[8];
    storage.getChars("cfg_access_local_fallback", fallbackBuf, sizeof(fallbackBuf));
    mem->localFallbackEnabled = (strcmp(fallbackBuf, "true") == 0 || strcmp(fallbackBuf, "1") == 0) ? 1 : 0;

    LOG_INFO("ACCESS", "INIT localFallback=%d", mem->localFallbackEnabled);
}

// ======================================================================
// MESSAGE DISPATCH
// ======================================================================

void ac_dispatchMessage(AccessControllerMemory* mem, const ServiceMessage& msg,
                        FingerprintDriver* fp, ServerClient* sc, DoorServiceMemory* ds) {
    switch (static_cast<AccessMsgType>(msg.type)) {
        case AccessMsgType::BEGIN_ENROLLMENT:
            ac_beginEnrollment(mem, fp, static_cast<int>(msg.u4.u1));
            break;
        case AccessMsgType::CANCEL_ENROLLMENT:
            ac_cancelEnrollment(mem, fp);
            break;
        case AccessMsgType::DELETE_FINGERPRINT:
            ac_deleteFingerprint(mem, fp, static_cast<int>(msg.u4.u1));
            break;
        case AccessMsgType::DELETE_ALL_FP:
            if (fp && fp->isOperational()) {
                fp->deleteAll();
                ac_publishEvent(DomainEvent::FINGERPRINT_DELETED, -1, 0, "", "all deleted");
            }
            break;
        case AccessMsgType::REMOTE_UNLOCK:
            ac_remoteUnlock(mem, ds);
            break;
        default:
            break;
    }
    mem->messagesProcessed++;
}

// ======================================================================
// MAIN UPDATE — state machine tick
// ======================================================================

void ac_update(AccessControllerMemory* mem, FingerprintDriver* fp,
               ServerClient* sc, DoorServiceMemory* ds) {
    // Drain mailbox
    ServiceMessage msg;
    while (g_registry.tryReceive(ServiceId::ACCESS_CONTROLLER, msg)) {
        ac_dispatchMessage(mem, msg, fp, sc, ds);
    }

    // Check if door closed while UNLOCKED → return to IDLE
    if (mem->state == AC_UNLOCKED && ds && !ds_isDoorOpen(ds)) {
        ac_transition(mem, AC_IDLE);
        return;
    }

    switch (mem->state) {
        case AC_IDLE:            ac_handleIdle(mem, fp);        break;
        case AC_SCANNING:        ac_handleScanning(mem, fp, sc); break;
        case AC_CHECKING_SERVER: ac_handleServerCheck(mem, sc);  break;
        case AC_LOCAL_AUTH_CHECK: ac_handleLocalAuthCheck(mem);   break;
        case AC_GRANTED:         ac_handleGranted(mem, ds);      break;
        case AC_DENIED:          ac_handleDenied(mem);           break;
        case AC_UNLOCKING:       ac_handleUnlocking(mem, ds);    break;
        case AC_UNLOCKED:        ac_handleUnlocked(mem, ds);     break;
        case AC_LOCKING:         ac_handleLocking(mem, ds);      break;
        case AC_ENROLLING:       ac_handleEnrolling(mem, fp);    break;
    }
}

// ======================================================================
// STATE HANDLERS
// ======================================================================

static void ac_handleIdle(AccessControllerMemory* mem, FingerprintDriver* fp) {
    if (fp && fp->isOperational()) {
        fp->startScan();
        ac_transition(mem, AC_SCANNING);
    }
}

static void ac_handleScanning(AccessControllerMemory* mem, FingerprintDriver* fp,
                               ServerClient* sc) {
    if (!fp || !fp->isOperational()) {
        ac_transition(mem, AC_IDLE);
        return;
    }

    int result = fp->checkScan();

    if (result >= 0) {
        mem->lastFpId = result;
        LOG_INFO("ACCESS", "SCAN fpId=%d", result);

        ac_publishEvent(DomainEvent::FINGERPRINT_SCAN, result, 0, "", "");

        // Local auth FIRST, then optional server check
        ac_transition(mem, AC_LOCAL_AUTH_CHECK);
    }
    // -1 = no finger, -2 = no match → stay scanning (don't spam)
}

static void ac_handleServerCheck(AccessControllerMemory* mem, ServerClient* sc) {
    if (!sc) {
        ac_transition(mem, AC_LOCAL_AUTH_CHECK);
        return;
    }

    int result = sc->checkAccess(mem->lastFpId);

    if (result == 1) {
        // Server says allow
        const char* userName = sc->getLastUserName();
        int userId           = sc->getLastUserId();
        const char* reason   = sc->getLastReason();

        snprintf(mem->currentUnlockUserName, sizeof(mem->currentUnlockUserName),
                 "%s", userName);
        mem->currentUnlockFpId   = mem->lastFpId;
        mem->currentUnlockUserId = userId;

        LOG_INFO("ACCESS", "GRANTED fpId=%d userId=%d name=%s reason=%s",
                 mem->lastFpId, userId, userName, reason);
        ac_logAccess(mem, "granted", mem->lastFpId, userId, userName, reason);
        ac_publishEvent(DomainEvent::ACCESS_GRANTED, mem->lastFpId, userId, userName, reason);
        ac_transition(mem, AC_GRANTED);

    } else if (result == 0) {
        // Server says deny
        const char* reason   = sc->getLastReason();
        const char* userName = sc->getLastUserName();

        LOG_INFO("ACCESS", "DENIED fpId=%d reason=%s", mem->lastFpId, reason);
        ac_logAccess(mem, "denied", mem->lastFpId, 0, userName, reason);
        ac_publishEvent(DomainEvent::ACCESS_DENIED, mem->lastFpId, 0, "", reason);
        ac_transition(mem, AC_DENIED);

    } else {
        // Server unreachable — try local fallback
        LOG_WARN("ACCESS", "SERVER_UNREACHABLE falling back to local");

        EventPayload ev;
        ev.type = DomainEvent::SERVER_UNREACHABLE;
        ev.timestamp = time(nullptr);
        EventBus::getInstance()->publish(ev);

        if (mem->localFallbackEnabled) {
            ac_transition(mem, AC_LOCAL_AUTH_CHECK);
        } else {
            ac_logAccess(mem, "denied", mem->lastFpId, 0, "",
                         "server unreachable, fallback disabled");
            ac_publishEvent(DomainEvent::ACCESS_DENIED, mem->lastFpId, 0, "",
                            "server unavailable");
            ac_transition(mem, AC_DENIED);
        }
    }
}

static void ac_handleLocalAuthCheck(AccessControllerMemory* mem) {
    UserRepositoryMemory* urMem = g_registry.getUserRepository();
    if (!urMem) {
        // No local user repo — try server
        ServerClient* sc = nullptr;  // Handled below via state check
        ac_transition(mem, AC_CHECKING_SERVER);
        return;
    }

    User* user = ur_findByFingerprintId(urMem, &storage, mem->lastFpId);

    if (user && user->active) {
        // Local match — GRANTED immediately
        snprintf(mem->currentUnlockUserName, sizeof(mem->currentUnlockUserName),
                 "%s", user->name);
        mem->currentUnlockFpId   = mem->lastFpId;
        mem->currentUnlockUserId = user->id;

        LOG_INFO("ACCESS", "LOCAL_GRANTED fpId=%d userId=%d name=%s",
                 mem->lastFpId, user->id, user->name);
        ac_logAccess(mem, "granted", mem->lastFpId, user->id, user->name, "local auth");
        ac_publishEvent(DomainEvent::ACCESS_GRANTED, mem->lastFpId, user->id,
                        user->name, "local auth");
        ac_transition(mem, AC_GRANTED);

    } else {
        // No local match — try server fallback
        LOG_INFO("ACCESS", "LOCAL_NO_MATCH fpId=%d — checking server", mem->lastFpId);
        ac_transition(mem, AC_CHECKING_SERVER);
    }
}

static void ac_handleGranted(AccessControllerMemory* mem, DoorServiceMemory* ds) {
    // Notify StateManager of user login
    if (mem->currentUnlockUserId > 0) {
        ServiceMessage sm = ServiceMessage::cmd(ServiceId::STATE_MANAGER,
            static_cast<uint8_t>(StateMsgType::USER_LOGIN));
        sm.u4.u1 = static_cast<uint16_t>(mem->currentUnlockUserId);
        g_registry.send(ServiceId::STATE_MANAGER, sm);
    }

    // Send unlock command to DoorService via mailbox
    ServiceMessage dm = ServiceMessage::cmd(ServiceId::DOOR_SERVICE,
        static_cast<uint8_t>(DoorMsgType::UNLOCK));
    if (g_registry.send(ServiceId::DOOR_SERVICE, dm)) {
        ac_transition(mem, AC_UNLOCKING);
    } else {
        LOG_INFO("ACCESS", "GRANTED but no door mailbox - skipping unlock");
        ac_transition(mem, AC_IDLE);
    }

    (void)ds; // ds is accessed via mailbox, not directly here
}

static void ac_handleDenied(AccessControllerMemory* mem) {
    if (millis() - mem->stateStartMs >= DENIED_DISPLAY_TIME_MS) {
        ac_transition(mem, AC_IDLE);
    }
}

static void ac_handleUnlocking(AccessControllerMemory* mem, DoorServiceMemory* ds) {
    // DoorService controls relay timing — once unlock is acknowledged,
    // transition to UNLOCKED
    if (!ds || (static_cast<DoorState>(ds->state) == DoorState::UNLOCKING
             || static_cast<DoorState>(ds->state) == DoorState::UNLOCKED)) {
        ac_transition(mem, AC_UNLOCKED);
    }
}

static void ac_handleUnlocked(AccessControllerMemory* mem, DoorServiceMemory* ds) {
    // Wait for door to close and relay to auto-deactivate
    if (!ds || (!ds_isDoorOpen(ds)
             && static_cast<DoorState>(ds->state) == DoorState::LOCKED)) {
        ac_transition(mem, AC_IDLE);
    }
}

static void ac_handleLocking(AccessControllerMemory* mem, DoorServiceMemory* ds) {
    // Transitional — move to idle when door service reports locked
    if (!ds || static_cast<DoorState>(ds->state) == DoorState::LOCKED) {
        ac_transition(mem, AC_IDLE);
    }
}

static void ac_handleEnrolling(AccessControllerMemory* mem, FingerprintDriver* fp) {
    if (!fp || !fp->isOperational()) {
        mem->enrollStep = -2;
        mem->enrolling  = 0;
        ac_transition(mem, AC_IDLE);
        return;
    }

    int step = fp->checkEnrollStep();

    if (step == 2) {
        // Success
        mem->enrollStep = 2;
        mem->enrolling  = 0;
        LOG_INFO("ACCESS", "ENROLL_COMPLETE fpId=%d", mem->enrollingFpId);

        ac_publishEvent(DomainEvent::FINGERPRINT_ENROLL_COMPLETE,
                        mem->enrollingFpId, 0, "", "");
        ac_transition(mem, AC_IDLE);

    } else if (step == -2) {
        // Failed
        mem->enrollStep = -2;
        mem->enrolling  = 0;
        LOG_WARN("ACCESS", "ENROLL_FAIL fpId=%d", mem->enrollingFpId);
        ac_transition(mem, AC_IDLE);

    } else if (step == -1) {
        // Timeout
        mem->enrollStep = -1;
        mem->enrolling  = 0;
        LOG_WARN("ACCESS", "ENROLL_TIMEOUT fpId=%d", mem->enrollingFpId);
        ac_transition(mem, AC_IDLE);

    } else {
        // 0 = waiting for first image, 1 = waiting for second
        mem->enrollStep = step;

        ac_publishEvent(DomainEvent::FINGERPRINT_ENROLL_STEP,
                        mem->enrollingFpId, step, "", "");
    }
}

// ======================================================================
// COMMANDS
// ======================================================================

bool ac_beginEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId) {
    if (!fp || !fp->isOperational()) return false;
    if (fpId < 1 || fpId > Config::MAX_FINGERPRINTS) return false;
    if (mem->state != AC_IDLE) return false;

    if (fp->startEnroll(fpId)) {
        mem->enrolling     = 1;
        mem->enrollingFpId = fpId;
        mem->enrollStep    = 0;
        ac_transition(mem, AC_ENROLLING);

        ac_publishEvent(DomainEvent::FINGERPRINT_ENROLL_START, fpId, 0, "", "");
        LOG_INFO("ACCESS", "ENROLL_START fpId=%d", fpId);
        return true;
    }
    return false;
}

bool ac_cancelEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp) {
    if (!mem->enrolling) return false;
    if (fp) fp->cancelEnroll();
    mem->enrolling  = 0;
    mem->enrollStep = -1;
    ac_transition(mem, AC_IDLE);
    return true;
}

bool ac_deleteFingerprint(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId) {
    if (!fp || !fp->isOperational()) return false;

    bool ok = fp->deleteFingerprint(fpId);
    if (ok) {
        ac_publishEvent(DomainEvent::FINGERPRINT_DELETED, fpId, 0, "", "");
    }
    return ok;
}

bool ac_remoteUnlock(AccessControllerMemory* mem, DoorServiceMemory* ds) {
    if (mem->state != AC_IDLE) return false;
    if (!ds) return false;

    mem->lastFpId             = 0;
    mem->currentUnlockFpId    = 0;
    mem->currentUnlockUserId  = 0;
    mem->currentUnlockUserName[0] = '\0';

    ac_logAccess(mem, "remote_unlock", 0, 0, "admin", "remote unlock via API");
    ac_publishEvent(DomainEvent::ACCESS_GRANTED, 0, 0, "admin", "remote unlock");
    ac_transition(mem, AC_GRANTED);
    return true;
}

// ======================================================================
// STATUS QUERIES
// ======================================================================

int ac_getState(const AccessControllerMemory* mem) {
    return static_cast<int>(mem->state);
}

const char* ac_getStateName(const AccessControllerMemory* mem) {
    static const char* names[] = {
        "idle", "scanning", "checking_server", "local_auth",
        "granted", "denied", "unlocking", "unlocked", "locking", "enrolling"
    };
    return (mem->state < 10) ? names[mem->state] : "unknown";
}

int ac_getLastFpId(const AccessControllerMemory* mem) {
    return mem->lastFpId;
}

const char* ac_getLastEvent(const AccessControllerMemory* mem) {
    return mem->lastEvent;
}

bool ac_isEnrolling(const AccessControllerMemory* mem) {
    return mem->enrolling != 0;
}

int ac_getEnrollStep(const AccessControllerMemory* mem) {
    return mem->enrollStep;
}

int ac_getEnrollingFpId(const AccessControllerMemory* mem) {
    return mem->enrollingFpId;
}

bool ac_isLocalFallbackEnabled(const AccessControllerMemory* mem) {
    return mem->localFallbackEnabled != 0;
}

int ac_getServerStatus(const AccessControllerMemory* mem, ServerClient* sc) {
    (void)mem;
    if (!sc || !sc->isConfigured()) return 2;
    return sc->isServerReachable() ? 0 : 1;
}

unsigned long ac_getServerFailDuration(const AccessControllerMemory* mem, ServerClient* sc) {
    (void)mem;
    return sc ? sc->getServerFailDuration() : 0;
}

unsigned long ac_getLastServerResponseTime(const AccessControllerMemory* mem, ServerClient* sc) {
    (void)mem;
    return sc ? sc->getLastResponseTime() : 0;
}

// ======================================================================
// INTERNAL HELPERS
// ======================================================================

static void ac_transition(AccessControllerMemory* mem, uint8_t newState) {
    mem->state        = newState;
    mem->stateStartMs  = millis();
}

static void ac_logAccess(AccessControllerMemory* mem, const char* decision,
                         int fpId, int userId, const char* userName, const char* reason) {
    snprintf(mem->lastEvent, sizeof(mem->lastEvent),
             "%s fpId=%d userId=%d user=%s reason=%s",
             decision, fpId, userId, userName, reason);
    LOG_INFO("ACCESS", "%s", mem->lastEvent);
}

static void ac_publishEvent(DomainEvent type, int fpId, int userId,
                             const char* userName, const char* reason) {
    EventPayload ev;
    ev.type                 = type;
    ev.timestamp            = time(nullptr);
    ev.data.access.fpId     = fpId;
    ev.data.access.userId   = userId;
    ev.data.access.userName = userName;
    ev.data.access.reason   = reason;
    EventBus::getInstance()->publish(ev);
}
