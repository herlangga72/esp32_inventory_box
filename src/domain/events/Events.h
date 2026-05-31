#ifndef DOMAIN_EVENTS_H
#define DOMAIN_EVENTS_H

#include <Arduino.h>
#include "../entities/Tool.h"
#include "../entities/User.h"

enum class DomainEvent {
    // Weight events
    WEIGHT_UPDATED,
    WEIGHT_STABLE,
    WEIGHT_CHANGE_SIGNIFICANT,
    
    // Motion events
    MOTION_DETECTED,
    MOTION_VIBRATION,
    MOTION_TILT,
    MOTION_FREE_FALL,
    MOTION_SETTLED,
    
    // Tool events
    TOOL_PLACED,
    TOOL_REMOVED,
    TOOL_MATCHED,
    TOOL_UNKNOWN,
    
    // User events
    USER_LOGIN,
    USER_LOGOUT,
    
    // Access events
    ACCESS_GRANTED,
    ACCESS_DENIED,
    ACCESS_LOCAL_FALLBACK,

    // Door events
    DOOR_OPEN,
    DOOR_CLOSE,
    DOOR_HELD_OPEN,

    // Fingerprint events
    FINGERPRINT_SCAN,
    FINGERPRINT_ENROLL_START,
    FINGERPRINT_ENROLL_STEP,
    FINGERPRINT_ENROLL_COMPLETE,
    FINGERPRINT_DELETED,

    // Server events
    SERVER_UNREACHABLE,
    SERVER_RECONNECTED,

    // System events
    STATE_CHANGED,
    CALIBRATION_STARTED,
    CALIBRATION_COMPLETE,
    SLEEP_ENTER,
    SLEEP_WAKE,
    ERROR_OCCURRED
};

enum class MotionType {
    NONE,
    VIBRATION,
    TILT,
    MOVEMENT,
    FREE_FALL,
    SETTLED
};

struct EventPayload {
    DomainEvent type;
    time_t timestamp;
    
    union {
        struct {
            float weight;
            float delta;
            float baseline;
        } weight;
        
        struct {
            int fpId;
            int userId;
            const char* userName;
            const char* reason;
        } access;

        struct {
            float ax, ay, az;
            MotionType motion;
        } motion;
        
        struct {
            int toolId;
            const char* name;
            float weight;
        } tool;
        
        struct {
            int userId;
            const char* name;
        } user;
        
        struct {
            int fromState;
            int toState;
        } state;
        
        struct {
            int errorCode;
            const char* message;
        } error;
        
        struct {
            float value;
        } generic;
    } data;
    
    EventPayload() : type(DomainEvent::WEIGHT_UPDATED), timestamp(0) {}
};

#endif // DOMAIN_EVENTS_H