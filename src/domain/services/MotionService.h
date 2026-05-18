#ifndef MOTION_SERVICE_H
#define MOTION_SERVICE_H

#include <Arduino.h>
#include "../hal/MPU6050Driver.h"
#include "../events/Events.h"
#include "config/Config.h"

class MotionService {
public:
    MotionService(MPU6050Driver* driver);
    
    void begin();
    void update();
    
    MotionType getCurrentMotion();
    void getAcceleration(float& ax, float& ay, float& az);
    bool isLidOpen();
    bool isBoxPickedUp();
    
    void calibrateBaseline();
    void getBaseline(float& bx, float& by, float& bz);
    void setBaseline(float bx, float by, float bz);
    
    bool isMotionDetected();

private:
    MPU6050Driver* mpu;
    
    float restingAccel[3];
    float currentAccel[3];
    MotionType currentMotion;
    
    MotionType classifyMotion(float ax, float ay, float az);
    bool detectTilt(float ax, float ay, float az);
    bool detectFreeFall(float magnitude);
};

#endif // MOTION_SERVICE_H