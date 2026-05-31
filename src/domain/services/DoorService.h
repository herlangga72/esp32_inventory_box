#ifndef DOOR_SERVICE_H
#define DOOR_SERVICE_H

#include <Arduino.h>
#include "../events/EventBus.h"

enum class DoorState {
    LOCKED,
    UNLOCKING,
    UNLOCKED,
    HELD_OPEN
};

class DoorService {
public:
    DoorService(EventBus* events);

    void begin();
    void update();           // called periodically, debounces reed + manages relay timer

    // Commands
    void unlock();           // energize relay for configured duration
    void lock();             // manually de-energize relay

    // State
    bool isDoorOpen() const;        // debounced reed switch state
    bool isRelayActive() const;     // is relay currently energized
    unsigned long getSecondsOpen() const;
    unsigned long getLockTimeRemaining() const;  // ms until auto-lock
    DoorState getState() const;

    // Configuration
    void setLockDuration(unsigned long ms);
    unsigned long getLockDuration() const;

private:
    EventBus* events;
    DoorState state;
    unsigned long stateStartMs;
    unsigned long relayActivateTime;
    unsigned long lockDurationMs;
    bool lastReedState;
    unsigned long lastReedChange;

    void setState(DoorState newState);
    bool readReedDebounced();  // 50ms debounce

    static const int DOOR_HELD_OPEN_SECS = 30;  // alarm threshold
};

#endif // DOOR_SERVICE_H
