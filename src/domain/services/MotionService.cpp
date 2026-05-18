#include "MotionService.h"
#include "../events/EventBus.h"
#include <math.h>

MotionService::MotionService(MPU6050Driver* driver)
    : mpu(driver), currentMotion(MotionType::NONE) {
    restingAccel[0] = restingAccel[1] = restingAccel[2] = 0;
    currentAccel[0] = currentAccel[1] = currentAccel[2] = 0;
}

void MotionService::begin() {
    mpu->begin();
}

void MotionService::update() {
    mpu->readAccel(currentAccel[0], currentAccel[1], currentAccel[2]);
    
    MotionType newMotion = classifyMotion(
        currentAccel[0] - restingAccel[0],
        currentAccel[1] - restingAccel[1],
        currentAccel[2] - restingAccel[2]
    );
    
    if (newMotion != currentMotion) {
        currentMotion = newMotion;
        
        // Publish motion event
        EventPayload event;
        event.type = DomainEvent::MOTION_DETECTED;
        event.timestamp = millis();
        event.data.motion.ax = currentAccel[0];
        event.data.motion.ay = currentAccel[1];
        event.data.motion.az = currentAccel[2];
        event.data.motion.motion = currentMotion;
        
        EventBus::getInstance()->publish(event);
    }
}

MotionType MotionService::classifyMotion(float dx, float dy, float dz) {
    float magnitude = sqrt(dx*dx + dy*dy + dz*dz);
    float zDelta = abs(currentAccel[2] - restingAccel[2]);
    
    // Free fall (all axes near 0)
    float totalMag = sqrt(currentAccel[0]*currentAccel[0] + 
                          currentAccel[1]*currentAccel[1] + 
                          currentAccel[2]*currentAccel[2]);
    if (totalMag < FREE_FALL_THRESHOLD_G) {
        return MotionType::FREE_FALL;
    }
    
    // Significant tilt (Z-axis change)
    if (zDelta > TILT_THRESHOLD_G) {
        return MotionType::TILT;
    }
    
    // Vibration or movement
    if (magnitude > MOTION_THRESHOLD_G) {
        if (magnitude > 0.5f) {
            return MotionType::MOVEMENT;
        }
        return MotionType::VIBRATION;
    }
    
    // Settled (baseline + noise)
    return MotionType::SETTLED;
}

bool MotionService::detectTilt(float ax, float ay, float az) {
    float zDelta = abs(az - restingAccel[2]);
    return zDelta > TILT_THRESHOLD_G;
}

bool MotionService::detectFreeFall(float magnitude) {
    return magnitude < FREE_FALL_THRESHOLD_G;
}

MotionType MotionService::getCurrentMotion() {
    return currentMotion;
}

void MotionService::getAcceleration(float& ax, float& ay, float& az) {
    ax = currentAccel[0];
    ay = currentAccel[1];
    az = currentAccel[2];
}

bool MotionService::isLidOpen() {
    // Lid open = significant tilt in Z-axis
    return detectTilt(currentAccel[0] - restingAccel[0],
                     currentAccel[1] - restingAccel[1],
                     currentAccel[2] - restingAccel[2]);
}

bool MotionService::isBoxPickedUp() {
    // Box picked up = significant movement
    return currentMotion == MotionType::MOVEMENT;
}

void MotionService::calibrateBaseline() {
    mpu->readAccel(restingAccel[0], restingAccel[1], restingAccel[2]);
    
    // Z should be ~1g when flat
    // restingAccel[2] -= 1.0f;  // Remove gravity component
    
    EventBus::getInstance()->publish(DomainEvent::CALIBRATION_COMPLETE);
}

void MotionService::getBaseline(float& bx, float& by, float& bz) {
    bx = restingAccel[0];
    by = restingAccel[1];
    bz = restingAccel[2];
}

void MotionService::setBaseline(float bx, float by, float bz) {
    restingAccel[0] = bx;
    restingAccel[1] = by;
    restingAccel[2] = bz;
}

bool MotionService::isMotionDetected() {
    return currentMotion != MotionType::NONE && 
           currentMotion != MotionType::SETTLED;
}