#include "AccessController.h"
#include "../../hal/FingerprintDriver.h"
#include "../../kernel/ServerClient.h"
#include "DoorService.h"
#include "../../data/UserRepository.h"
#include "../../data/StorageManager.h"
#include "../../config/Config.h"
#include "../../utils/LogManager.h"

AccessController::AccessController(EventBus* events)
    : events(events),
      fpDriver(nullptr), serverClient(nullptr), doorService(nullptr),
      userRepo(nullptr), storage(nullptr),
      state(AccessState::IDLE), stateStartMs(0), lastFpId(0),
      enrolling(false), enrollingFpId(0), enrollStep(-1),
      localFallbackEnabled(true),
      currentUnlockFpId(0), currentUnlockUserId(0) {
    lastEvent[0] = '\0';
    currentUnlockUserName[0] = '\0';
}

void AccessController::begin() {
    stateStartMs = millis();

    // Load config from NVS
    if (storage) {
        String fallbackStr = storage->getString("cfg_access_local_fallback", "true");
        localFallbackEnabled = (fallbackStr == "true" || fallbackStr == "1");
    }

    LOG_INFO("ACCESS", "INIT localFallback=%d", localFallbackEnabled);
}

void AccessController::update() {
    // Check if door needs re-locking (door closed + state is UNLOCKED)
    if (state == AccessState::UNLOCKED && doorService && !doorService->isDoorOpen()) {
        transition(AccessState::IDLE);
        return;
    }

    switch (state) {
    case AccessState::IDLE:          handleIdle(); break;
    case AccessState::SCANNING:      handleScanning(); break;
    case AccessState::CHECKING_SERVER: handleServerCheck(); break;
    case AccessState::LOCAL_AUTH_CHECK: handleLocalAuthCheck(); break;
    case AccessState::GRANTED:       handleGranted(); break;
    case AccessState::DENIED:        handleDenied(); break;
    case AccessState::UNLOCKING:     handleUnlocking(); break;
    case AccessState::UNLOCKED:      handleUnlocked(); break;
    case AccessState::LOCKING:       handleLocking(); break;
    case AccessState::ENROLLING:     handleEnrolling(); break;
    }
}

// ---- State Handlers ----

void AccessController::handleIdle() {
    // Start scanning if fingerprint driver is operational
    if (fpDriver && fpDriver->isOperational()) {
        fpDriver->startScan();
        transition(AccessState::SCANNING);
    }
}

void AccessController::handleScanning() {
    if (!fpDriver || !fpDriver->isOperational()) {
        transition(AccessState::IDLE);
        return;
    }

    int result = fpDriver->checkScan();

    if (result >= 0) {
        // Fingerprint matched
        lastFpId = result;
        LOG_INFO("ACCESS", "SCAN fpId=%d", result);

        EventPayload ev;
        ev.type = DomainEvent::FINGERPRINT_SCAN;
        ev.timestamp = time(nullptr);
        ev.data.access.fpId = result;
        ev.data.access.userId = 0;
        ev.data.access.userName = "";
        ev.data.access.reason = "";
        events->publish(ev);

        // Check with server first (or local if server not configured)
        if (serverClient && serverClient->isConfigured()) {
            transition(AccessState::CHECKING_SERVER);
        } else {
            transition(AccessState::LOCAL_AUTH_CHECK);
        }
    } else if (result == -2) {
        // No match
        LOG_INFO("ACCESS", "SCAN_NO_MATCH fpId=none");
        // Stay in scanning, don't spam
    }
    // -1 = no finger, keep scanning
}

void AccessController::handleServerCheck() {
    if (!serverClient) {
        transition(AccessState::LOCAL_AUTH_CHECK);
        return;
    }

    int result = serverClient->checkAccess(lastFpId);

    if (result == 1) {
        // Server says allow
        const char* userName = serverClient->getLastUserName();
        int userId = serverClient->getLastUserId();
        const char* reason = serverClient->getLastReason();

        snprintf(currentUnlockUserName, sizeof(currentUnlockUserName), "%s", userName);
        currentUnlockFpId = lastFpId;
        currentUnlockUserId = userId;

        LOG_INFO("ACCESS", "GRANTED fpId=%d userId=%d name=%s reason=%s",
                 lastFpId, userId, userName, reason);
        logAccess("granted", lastFpId, userId, userName, reason);
        publishAccessEvent(DomainEvent::ACCESS_GRANTED, lastFpId, userId, userName, reason);
        transition(AccessState::GRANTED);
    } else if (result == 0) {
        // Server says deny
        const char* reason = serverClient->getLastReason();
        const char* userName = serverClient->getLastUserName();
        LOG_INFO("ACCESS", "DENIED fpId=%d reason=%s", lastFpId, reason);
        logAccess("denied", lastFpId, 0, userName, reason);
        publishAccessEvent(DomainEvent::ACCESS_DENIED, lastFpId, 0, "", reason);
        transition(AccessState::DENIED);
    } else {
        // Server unreachable — try local fallback
        LOG_WARN("ACCESS", "SERVER_UNREACHABLE falling back to local");
        EventPayload ev;
        ev.type = DomainEvent::SERVER_UNREACHABLE;
        ev.timestamp = time(nullptr);
        events->publish(ev);

        if (localFallbackEnabled) {
            transition(AccessState::LOCAL_AUTH_CHECK);
        } else {
            logAccess("denied", lastFpId, 0, "", "server unreachable, fallback disabled");
            publishAccessEvent(DomainEvent::ACCESS_DENIED, lastFpId, 0, "", "server unavailable");
            transition(AccessState::DENIED);
        }
    }
}

void AccessController::handleLocalAuthCheck() {
    if (!userRepo) {
        logAccess("denied", lastFpId, 0, "", "no user repo");
        transition(AccessState::DENIED);
        return;
    }

    User* user = userRepo->findByFingerprintId(lastFpId);

    if (user && user->active) {
        snprintf(currentUnlockUserName, sizeof(currentUnlockUserName), "%s", user->name);
        currentUnlockFpId = lastFpId;
        currentUnlockUserId = user->id;

        LOG_INFO("ACCESS", "LOCAL_FALLBACK fpId=%d userId=%d name=%s",
                 lastFpId, user->id, user->name);
        logAccess("local_fallback", lastFpId, user->id, user->name, "server unreachable");
        publishAccessEvent(DomainEvent::ACCESS_LOCAL_FALLBACK, lastFpId, user->id, user->name,
                          "local fallback");
        transition(AccessState::GRANTED);
    } else {
        LOG_INFO("ACCESS", "DENIED_LOCAL fpId=%d no matching user", lastFpId);
        logAccess("denied", lastFpId, 0, "", "no matching local user");
        publishAccessEvent(DomainEvent::ACCESS_DENIED, lastFpId, 0, "", "no matching user");
        transition(AccessState::DENIED);
    }
}

void AccessController::handleGranted() {
    if (doorService) {
        doorService->unlock();
        transition(AccessState::UNLOCKING);
    } else {
        // No door hardware — log and skip to idle
        LOG_INFO("ACCESS", "GRANTED but no door hardware - skipping unlock");
        transition(AccessState::IDLE);
    }
}

void AccessController::handleDenied() {
    // Hold denied state briefly for logging/display
    if (millis() - stateStartMs >= DENIED_DISPLAY_TIME_MS) {
        transition(AccessState::IDLE);
    }
}

void AccessController::handleUnlocking() {
    // DoorService controls relay timing — wait for state change
    if (!doorService || doorService->isRelayActive()) {
        transition(AccessState::UNLOCKED);
    }
}

void AccessController::handleUnlocked() {
    // Wait for door to close, then relock
    if (!doorService || (!doorService->isDoorOpen() && !doorService->isRelayActive())) {
        transition(AccessState::IDLE);
    }
    // Auto-relock handled by DoorService timer
}

void AccessController::handleLocking() {
    // Transitional — move to idle when done
    if (!doorService || !doorService->isRelayActive()) {
        transition(AccessState::IDLE);
    }
}

void AccessController::handleEnrolling() {
    if (!fpDriver || !fpDriver->isOperational()) {
        enrollStep = -2;
        enrolling = false;
        transition(AccessState::IDLE);
        return;
    }

    int step = fpDriver->checkEnrollStep();

    if (step == 2) {
        // Success
        enrollStep = 2;
        enrolling = false;
        LOG_INFO("ACCESS", "ENROLL_COMPLETE fpId=%d", enrollingFpId);

        EventPayload ev;
        ev.type = DomainEvent::FINGERPRINT_ENROLL_COMPLETE;
        ev.timestamp = time(nullptr);
        ev.data.access.fpId = enrollingFpId;
        ev.data.access.userId = 0;
        ev.data.access.userName = "";
        ev.data.access.reason = "";
        events->publish(ev);

        transition(AccessState::IDLE);
    } else if (step == -2) {
        // Failed
        enrollStep = -2;
        enrolling = false;
        LOG_WARN("ACCESS", "ENROLL_FAIL fpId=%d", enrollingFpId);
        transition(AccessState::IDLE);
    } else if (step == -1) {
        // Timeout
        enrollStep = -1;
        enrolling = false;
        LOG_WARN("ACCESS", "ENROLL_TIMEOUT fpId=%d", enrollingFpId);
        transition(AccessState::IDLE);
    } else {
        // 0 = waiting for first image, 1 = waiting for second
        enrollStep = step;

        if (step == 1 && enrollStep == 0) {
            // First image captured, moving to second
        }
        // Publishing step event
        EventPayload ev;
        ev.type = DomainEvent::FINGERPRINT_ENROLL_STEP;
        ev.timestamp = time(nullptr);
        ev.data.access.fpId = enrollingFpId;
        ev.data.access.userId = step;
        ev.data.access.reason = "";
        events->publish(ev);
    }
}

// ---- Commands ----

bool AccessController::beginEnrollment(int fpId) {
    if (!fpDriver || !fpDriver->isOperational()) return false;
    if (fpId < 1 || fpId > Config::MAX_FINGERPRINTS) return false;
    if (state != AccessState::IDLE) return false;

    if (fpDriver->startEnroll(fpId)) {
        enrolling = true;
        enrollingFpId = fpId;
        enrollStep = 0;
        transition(AccessState::ENROLLING);

        EventPayload ev;
        ev.type = DomainEvent::FINGERPRINT_ENROLL_START;
        ev.timestamp = time(nullptr);
        ev.data.access.fpId = fpId;
        events->publish(ev);

        LOG_INFO("ACCESS", "ENROLL_START fpId=%d", fpId);
        return true;
    }
    return false;
}

bool AccessController::cancelEnrollment() {
    if (!enrolling) return false;
    fpDriver->cancelEnroll();
    enrolling = false;
    enrollStep = -1;
    transition(AccessState::IDLE);
    return true;
}

bool AccessController::deleteFingerprint(int fpId) {
    if (!fpDriver || !fpDriver->isOperational()) return false;

    bool ok = fpDriver->deleteFingerprint(fpId);
    if (ok) {
        EventPayload ev;
        ev.type = DomainEvent::FINGERPRINT_DELETED;
        ev.timestamp = time(nullptr);
        ev.data.access.fpId = fpId;
        events->publish(ev);
    }
    return ok;
}

bool AccessController::deleteAllFingerprints() {
    if (!fpDriver || !fpDriver->isOperational()) return false;
    return fpDriver->deleteAll();
}

bool AccessController::remoteUnlock() {
    if (state != AccessState::IDLE) return false;
    if (!doorService) return false;

    lastFpId = 0;
    currentUnlockFpId = 0;
    currentUnlockUserId = 0;
    currentUnlockUserName[0] = '\0';

    logAccess("remote_unlock", 0, 0, "admin", "remote unlock via API");
    publishAccessEvent(DomainEvent::ACCESS_GRANTED, 0, 0, "admin", "remote unlock");
    transition(AccessState::GRANTED);
    return true;
}

// ---- Enrollment Status ----

int AccessController::getEnrollStep() {
    return enrollStep;
}

int AccessController::getEnrollingFpId() {
    return enrollingFpId;
}

// ---- Status ----

AccessState AccessController::getState() const {
    return state;
}

const char* AccessController::getStateName() const {
    switch (state) {
    case AccessState::IDLE:            return "idle";
    case AccessState::SCANNING:        return "scanning";
    case AccessState::CHECKING_SERVER: return "checking_server";
    case AccessState::LOCAL_AUTH_CHECK: return "local_auth";
    case AccessState::GRANTED:         return "granted";
    case AccessState::DENIED:          return "denied";
    case AccessState::UNLOCKING:       return "unlocking";
    case AccessState::UNLOCKED:        return "unlocked";
    case AccessState::LOCKING:         return "locking";
    case AccessState::ENROLLING:       return "enrolling";
    default: return "unknown";
    }
}

int AccessController::getLastFingerprintId() const {
    return lastFpId;
}

const char* AccessController::getLastEvent() const {
    return lastEvent;
}

bool AccessController::isEnrolling() const {
    return enrolling;
}

// ---- Configuration ----

bool AccessController::isLocalFallbackEnabled() const {
    return localFallbackEnabled;
}

void AccessController::setLocalFallbackEnabled(bool enabled) {
    localFallbackEnabled = enabled;
    if (storage) {
        storage->putString("cfg_access_local_fallback", enabled ? "true" : "false");
    }
}

unsigned long AccessController::getServerFailDuration() const {
    if (serverClient) {
        return serverClient->getServerFailDuration();
    }
    return 0;
}

unsigned long AccessController::getLastServerResponseTime() const {
    if (serverClient) {
        return serverClient->getLastResponseTime();
    }
    return 0;
}

int AccessController::getServerStatus() const {
    if (!serverClient || !serverClient->isConfigured()) return 2;
    return serverClient->isServerReachable() ? 0 : 1;
}

// ---- Dependency Injection ----

void AccessController::setFingerprintDriver(FingerprintDriver* fp) {
    fpDriver = fp;
}

void AccessController::setServerClient(ServerClient* sc) {
    serverClient = sc;
}

void AccessController::setDoorService(DoorService* ds) {
    doorService = ds;
}

void AccessController::setUserRepository(UserRepository* ur) {
    userRepo = ur;
}

void AccessController::setStorageManager(StorageManager* sm) {
    storage = sm;
}

// ---- Helpers ----

void AccessController::transition(AccessState newState) {
    state = newState;
    stateStartMs = millis();
}

void AccessController::logAccess(const char* decision, int fpId, int userId,
                                  const char* userName, const char* reason) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s fpId=%d userId=%d user=%s reason=%s",
             decision, fpId, userId, userName, reason);
    strncpy(lastEvent, buf, sizeof(lastEvent) - 1);
    lastEvent[sizeof(lastEvent) - 1] = '\0';

    LOG_INFO("ACCESS", "%s", buf);
}

void AccessController::publishAccessEvent(DomainEvent event, int fpId, int userId,
                                           const char* userName, const char* reason) {
    EventPayload ev;
    ev.type = event;
    ev.timestamp = time(nullptr);
    ev.data.access.fpId = fpId;
    ev.data.access.userId = userId;
    ev.data.access.userName = userName;
    ev.data.access.reason = reason;
    events->publish(ev);
}
