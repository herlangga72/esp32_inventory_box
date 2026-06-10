#include "MotionService.h"
#include "../events/EventBus.h"
#include "../../config/Config.h"
#include <math.h>

void ms_update(MotionServiceMemory* mem, MPU6050Driver* mpu) {
    // 1. Read current acceleration from the sensor
    mpu->readAccel(mem->currentAccel[0], mem->currentAccel[1], mem->currentAccel[2]);

    // 2. Compute deltas from the resting baseline
    float dx = mem->currentAccel[0] - mem->restingAccel[0];
    float dy = mem->currentAccel[1] - mem->restingAccel[1];
    float dz = mem->currentAccel[2] - mem->restingAccel[2];

    // 3. Classify motion (same logic as the original MotionService::update)
    float magnitude = sqrt(dx * dx + dy * dy + dz * dz);
    float zDelta    = fabs(mem->currentAccel[2] - mem->restingAccel[2]);

    float totalMag  = sqrt(mem->currentAccel[0] * mem->currentAccel[0] +
                           mem->currentAccel[1] * mem->currentAccel[1] +
                           mem->currentAccel[2] * mem->currentAccel[2]);

    MotionType newMotion;
    if (totalMag < Config::FREE_FALL_THRESHOLD_G) {
        newMotion = MotionType::FREE_FALL;
    } else if (zDelta > Config::TILT_THRESHOLD_G) {
        newMotion = MotionType::TILT;
    } else if (magnitude > Config::MOTION_THRESHOLD_G) {
        if (magnitude > 0.5f) {
            newMotion = MotionType::MOVEMENT;
        } else {
            newMotion = MotionType::VIBRATION;
        }
    } else {
        newMotion = MotionType::SETTLED;
    }

    // 4. If the motion type changed, dispatch events
    if (newMotion != static_cast<MotionType>(mem->currentMotion)) {
        mem->currentMotion = static_cast<uint8_t>(newMotion);

        // Send MOTION_DETECTED to StateManager mailbox
        ServiceMessage sm = ServiceMessage::cmd(
            ServiceId::STATE_MANAGER,
            static_cast<uint8_t>(StateMsgType::MOTION_DETECTED));
        sm.bytes.b0 = static_cast<uint8_t>(newMotion);
        g_registry.send(ServiceId::STATE_MANAGER, sm);

        // Publish MOTION_DETECTED to EventBus for backward compatibility
        EventPayload event;
        event.type = DomainEvent::MOTION_DETECTED;
        event.timestamp = millis();
        event.data.motion.ax     = mem->currentAccel[0];
        event.data.motion.ay     = mem->currentAccel[1];
        event.data.motion.az     = mem->currentAccel[2];
        event.data.motion.motion = newMotion;
        EventBus::getInstance()->publish(event);
    }

    mem->messagesProcessed++;
}
