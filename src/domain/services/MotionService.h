#ifndef MOTION_SERVICE_H
#define MOTION_SERVICE_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"
#include "../../hal/MPU6050Driver.h"

// Free function on MotionServiceMemory*
// Reads accelerometer, classifies motion, and dispatches events on change.
void ms_update(MotionServiceMemory* mem, MPU6050Driver* mpu);

#endif // MOTION_SERVICE_H
