#include "DoorService.h"
#include "../../config/Config.h"
#include "../../utils/LogManager.h"

DoorService::DoorService(EventBus* events)
    : events(events), state(DoorState::LOCKED), stateStartMs(0),
      relayActivateTime(0), lockDurationMs(RELAY_DURATION_MS),
      lastReedState(false), lastReedChange(0) {}

void DoorService::begin() {
    // Relay: active LOW = unlocked, HIGH = locked (safe-fail)
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE == LOW ? HIGH : LOW);  // lock on boot

    // Reed switch: internal pullup, LOW = magnet near = door closed
    pinMode(PIN_DOOR_SENSOR, INPUT_PULLUP);

    lastReedState = readReedDebounced();
    stateStartMs = millis();

    LOG_INFO("DOOR", "INIT state=%s reed=%s relay=%s",
             "LOCKED",
             lastReedState ? "open" : "closed",
             isRelayActive() ? "on" : "off");
}

void DoorService::update() {
    // Debounce reed switch
    bool currentReed = readReedDebounced();

    if (currentReed != lastReedState) {
        lastReedState = currentReed;

        if (currentReed) {
            // Door opened
            LOG_INFO("DOOR", "OPEN");
            EventPayload ev;
            ev.type = DomainEvent::DOOR_OPEN;
            ev.timestamp = time(nullptr);
            events->publish(ev);
        } else {
            // Door closed
            LOG_INFO("DOOR", "CLOSE");
            EventPayload ev;
            ev.type = DomainEvent::DOOR_CLOSE;
            ev.timestamp = time(nullptr);
            events->publish(ev);
        }
    }

    // Relay auto-lock timer
    if (state == DoorState::UNLOCKING && isRelayActive()) {
        if (millis() - relayActivateTime >= lockDurationMs) {
            lock();
            setState(DoorState::LOCKED);
        }
    }

    // Transition UNLOCKING -> UNLOCKED after relay fires
    if (state == DoorState::UNLOCKING && relayActivateTime > 0) {
        if (millis() - relayActivateTime > 100) {
            setState(DoorState::UNLOCKED);
        }
    }

    // Door held open alarm
    if (lastReedState && state != DoorState::HELD_OPEN) {
        unsigned long openSecs = getSecondsOpen();
        if (openSecs >= DOOR_HELD_OPEN_SECS) {
            LOG_WARN("DOOR", "HELD_OPEN duration=%ds", openSecs);
            setState(DoorState::HELD_OPEN);
            EventPayload ev;
            ev.type = DomainEvent::DOOR_HELD_OPEN;
            ev.timestamp = time(nullptr);
            events->publish(ev);
        }
    }

    // Reset held open when door closes
    if (!lastReedState && state == DoorState::HELD_OPEN) {
        setState(DoorState::LOCKED);
    }
}

void DoorService::unlock() {
    digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE);
    relayActivateTime = millis();
    setState(DoorState::UNLOCKING);
    LOG_INFO("DOOR", "UNLOCK duration=%dms", lockDurationMs);
}

void DoorService::lock() {
    digitalWrite(PIN_RELAY, RELAY_ACTIVE_STATE == LOW ? HIGH : LOW);
    relayActivateTime = 0;
}

bool DoorService::isDoorOpen() const {
    return lastReedState;
}

bool DoorService::isRelayActive() const {
    return digitalRead(PIN_RELAY) == RELAY_ACTIVE_STATE;
}

unsigned long DoorService::getSecondsOpen() const {
    if (!lastReedState) return 0;
    return (millis() - lastReedChange) / 1000;
}

unsigned long DoorService::getLockTimeRemaining() const {
    if (state != DoorState::UNLOCKING || relayActivateTime == 0) return 0;
    unsigned long elapsed = millis() - relayActivateTime;
    if (elapsed >= lockDurationMs) return 0;
    return lockDurationMs - elapsed;
}

DoorState DoorService::getState() const {
    return state;
}

void DoorService::setLockDuration(unsigned long ms) {
    lockDurationMs = ms;
}

unsigned long DoorService::getLockDuration() const {
    return lockDurationMs;
}

void DoorService::setState(DoorState newState) {
    state = newState;
    stateStartMs = millis();
}

bool DoorService::readReedDebounced() {
    // Simple majority-read debounce (3 reads, 10ms apart)
    int highs = 0;
    for (int i = 0; i < 3; i++) {
        if (digitalRead(PIN_DOOR_SENSOR) == HIGH) highs++;
        delay(10);
    }
    return highs >= 2;
}
