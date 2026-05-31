#ifndef ACCESS_CONTROLLER_H
#define ACCESS_CONTROLLER_H

#include <Arduino.h>
#include "../events/EventBus.h"

class FingerprintDriver;
class ServerClient;
class DoorService;
class UserRepository;
class StorageManager;

enum class AccessState {
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

class AccessController {
public:
    AccessController(EventBus* events);

    void begin();
    void update();  // main state machine tick (~5Hz from FreeRTOS task)

    // Dependencies (setter injection)
    void setFingerprintDriver(FingerprintDriver* fp);
    void setServerClient(ServerClient* sc);
    void setDoorService(DoorService* ds);
    void setUserRepository(UserRepository* ur);
    void setStorageManager(StorageManager* sm);

    // Commands from Web API
    bool beginEnrollment(int fpId);
    bool cancelEnrollment();
    bool deleteFingerprint(int fpId);
    bool deleteAllFingerprints();
    bool remoteUnlock();

    // Enrollment progress for UI
    int  getEnrollStep();         // -2=fail, -1=idle, 0=waiting, 1=second, 2=done
    int  getEnrollingFpId();

    // Status for API
    AccessState getState() const;
    const char* getStateName() const;
    int  getLastFingerprintId() const;
    const char* getLastEvent() const;
    bool isEnrolling() const;

    // Configuration
    bool isLocalFallbackEnabled() const;
    void setLocalFallbackEnabled(bool enabled);
    unsigned long getServerFailDuration() const;
    unsigned long getLastServerResponseTime() const;
    int  getServerStatus() const;  // 0=ok, 1=unreachable, 2=not configured

private:
    EventBus* events;
    FingerprintDriver* fpDriver;
    ServerClient* serverClient;
    DoorService* doorService;
    UserRepository* userRepo;
    StorageManager* storage;

    AccessState state;
    unsigned long stateStartMs;
    int  lastFpId;
    char lastEvent[64];

    // Enrollment state
    bool enrolling;
    int  enrollingFpId;
    int  enrollStep;

    // Local fallback config
    bool localFallbackEnabled;

    // Unlock host (finger ID that triggered current unlock)
    int  currentUnlockFpId;
    int  currentUnlockUserId;
    char currentUnlockUserName[32];

    // State handlers
    void handleIdle();
    void handleScanning();
    void handleServerCheck();
    void handleLocalAuthCheck();
    void handleGranted();
    void handleDenied();
    void handleUnlocking();
    void handleUnlocked();
    void handleLocking();
    void handleEnrolling();

    void transition(AccessState newState);
    void logAccess(const char* decision, int fpId, int userId, const char* userName, const char* reason);
    void publishAccessEvent(DomainEvent event, int fpId, int userId, const char* userName, const char* reason);

    static const unsigned long LOCAL_FALLBACK_TIMEOUT_MS = 5000;   // wait for server before fallback
    static const unsigned long GRANTED_DISPLAY_TIME_MS = 2000;     // hold granted state for display
    static const unsigned long DENIED_DISPLAY_TIME_MS = 2000;      // hold denied state for display
};

#endif // ACCESS_CONTROLLER_H
