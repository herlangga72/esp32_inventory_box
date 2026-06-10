#include "DoorService.h"
#include "../events/EventBus.h"
#include "../../config/Config.h"
#include "../../utils/LogManager.h"
#include <Arduino.h>

// ---- Constants ----
static const int DOOR_HELD_OPEN_SECS = 30;

// Internal state enum (matches DoorState)
enum : uint8_t { DS_LOCKED, DS_UNLOCKING, DS_UNLOCKED, DS_HELD_OPEN };

// ---- Helper ----
static bool ds_readReedDebounced() {
    int highs = 0;
    for (int i = 0; i < 3; i++) {
        if (digitalRead(PIN_DOOR_SENSOR) == 1) highs++;
        delay(10);
    }
    return (highs >= 2);
}

// ======================================================================
// LIFECYCLE
// ======================================================================

void ds_begin(DoorServiceMemory* mem) {
    memset(mem, 0, sizeof(DoorServiceMemory));
    mem->state          = DS_LOCKED;
    mem->lockDurationMs = RELAY_DURATION_MS;

    // Relay: active 0 = unlocked, 1 = locked (safe-fail)
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE == 0 ? 1 : 0);

    // Reed switch: internal pullup, 0 = magnet near = door closed
    pinMode(PIN_DOOR_SENSOR, INPUT_PULLUP);

    // Read initial reed state with debounce
    mem->lastReedState = ds_readReedDebounced();
    mem->stateStartMs  = millis();

    LOG_INFO("DOOR", "INIT state=LOCKED reed=%s relay=%s",
             mem->lastReedState ? "open" : "closed",
             digitalRead(PIN_RELAY) == RELAY_ACTIVE_STATE ? "on" : "off");
}

// ======================================================================
// MESSAGE DISPATCH
// ======================================================================

void ds_dispatchMessage(DoorServiceMemory* mem, const ServiceMessage& msg) {
    switch (static_cast<DoorMsgType>(msg.type)) {
        case DoorMsgType::UNLOCK:
            // Optional duration override
            if (msg.u32.u32_1 > 0) {
                mem->lockDurationMs = msg.u32.u32_1;
            }
            // Energize relay
            digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE);
            mem->relayActivateTime = millis();
            mem->state = DS_UNLOCKING;
            mem->stateStartMs = millis();
            LOG_INFO("DOOR", "UNLOCK duration=%dms", (int)mem->lockDurationMs);
            break;

        case DoorMsgType::LOCK:
            // De-energize relay (safe-fail: opposite of active state)
            digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE == 0 ? 1 : 0);
            mem->relayActivateTime = 0;
            mem->state = DS_LOCKED;
            mem->stateStartMs = millis();
            LOG_INFO("DOOR", "LOCK");
            break;

        case DoorMsgType::SET_DURATION:
            mem->lockDurationMs = msg.u32.u32_1;
            LOG_INFO("DOOR", "SET_DURATION %dms", (int)mem->lockDurationMs);
            break;

        default:
            break;
    }
}

// ======================================================================
// MAIN UPDATE — periodic tick
// ======================================================================

void ds_update(DoorServiceMemory* mem) {
    // Drain mailbox
    ServiceMessage msg;
    while (g_registry.tryReceive(ServiceId::DOOR_SERVICE, msg)) {
        ds_dispatchMessage(mem, msg);
    }

    // Debounce reed switch
    bool currentReed = ds_readReedDebounced();

    if (currentReed != mem->lastReedState) {
        mem->lastReedState  = currentReed;
        mem->lastReedChange = millis();

        if (currentReed) {
            // Door opened
            LOG_INFO("DOOR", "OPEN");
            EventPayload ev;
            ev.type = DomainEvent::DOOR_OPEN;
            ev.timestamp = time(nullptr);
            EventBus::getInstance()->publish(ev);
        } else {
            // Door closed
            LOG_INFO("DOOR", "CLOSE");
            EventPayload ev;
            ev.type = DomainEvent::DOOR_CLOSE;
            ev.timestamp = time(nullptr);
            EventBus::getInstance()->publish(ev);
        }
    }

    // Auto-lock timer — check while relay is energized
    if ((mem->state == DS_UNLOCKING || mem->state == DS_UNLOCKED)
        && digitalRead(PIN_RELAY) == RELAY_ACTIVE_STATE) {
        if (millis() - mem->relayActivateTime >= mem->lockDurationMs) {
            digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE == 0 ? 1 : 0);
            mem->relayActivateTime = 0;
            mem->state = DS_LOCKED;
            mem->stateStartMs = millis();
            LOG_INFO("DOOR", "AUTO_LOCK");
        }
    }

    // Transition UNLOCKING -> UNLOCKED after relay fires (100ms debounce)
    if (mem->state == DS_UNLOCKING && mem->relayActivateTime > 0) {
        if (millis() - mem->relayActivateTime > 100) {
            mem->state = DS_UNLOCKED;
            mem->stateStartMs = millis();
        }
    }

    // Door held-open alarm (> 30s)
    if (mem->lastReedState && mem->state != DS_HELD_OPEN) {
        unsigned long openSecs = (millis() - mem->lastReedChange) / 1000;
        if (openSecs >= static_cast<unsigned long>(DOOR_HELD_OPEN_SECS)) {
            LOG_WARN("DOOR", "HELD_OPEN duration=%ds", (int)openSecs);
            mem->state = DS_HELD_OPEN;
            mem->stateStartMs = millis();

            EventPayload ev;
            ev.type = DomainEvent::DOOR_HELD_OPEN;
            ev.timestamp = time(nullptr);
            EventBus::getInstance()->publish(ev);
        }
    }

    // Reset held-open when door closes
    if (!mem->lastReedState && mem->state == DS_HELD_OPEN) {
        mem->state = DS_LOCKED;
        mem->stateStartMs = millis();
    }
}

// ======================================================================
// STATUS QUERY
// ======================================================================

bool ds_isDoorOpen(const DoorServiceMemory* mem) {
    return mem->lastReedState;
}
