#ifndef DOOR_SERVICE_H
#define DOOR_SERVICE_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"

enum class DoorState : uint8_t {
    LOCKED,
    UNLOCKING,
    UNLOCKED,
    HELD_OPEN
};

void ds_begin(DoorServiceMemory* mem);
void ds_dispatchMessage(DoorServiceMemory* mem, const ServiceMessage& msg);
void ds_update(DoorServiceMemory* mem);
bool ds_isDoorOpen(const DoorServiceMemory* mem);

#endif // DOOR_SERVICE_H
