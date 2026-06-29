---
type: Domain Logic
title: State Machines
description: All four state machines — BoxStateManager (weight tracking), AccessController (fingerprint auth), DoorService (lock control), PowerManager (sleep modes).
tags: [state-machine, domain, access-control, power]
timestamp: 2026-06-29T00:00:00Z
---

# State Machines

## 1. StateManager — Box State Machine

Tracks tool placement/removal via weight + motion sensing.

```
   INIT ──► IDLE ──► ANALYZING ──► TOOL_PLACED ──► REMOVING ──┐
              ▲         │               │             ▲        │
              │         ▼               └─────────────┘        │
              │  UNKNOWN_ITEM ────────────────────────────────┘
              │         ▲
              │  CALIBRATING  (legacy path)
              │  SLEEP ──► IDLE (on WAKE)
              └── (door close re-eval)
```

| State | Value | Meaning |
|-------|-------|---------|
| INIT | 0 | Post-init, transitions to IDLE immediately |
| IDLE | 1 | Waiting for weight change exceeding threshold |
| ANALYZING | 2 | Weight changed, awaiting motion SETTLED |
| TOOL_PLACED | 3 | Tool matched and added to contents |
| REMOVING | 4 | Weight decreased below threshold |
| UNKNOWN_ITEM | 5 | Weight present but no tool match |
| CALIBRATING | 6 | Baseline calibration (legacy) |
| SLEEP | 8 | System sleeping |

### Key Transitions

| Trigger | Condition | From → To |
|---------|-----------|-----------|
| WEIGHT_CHANGE | `\|delta\| > threshold` (2.0g) | IDLE → ANALYZING |
| MOTION_DETECTED(SETTLED) | Tool match found | ANALYZING → TOOL_PLACED |
| MOTION_DETECTED(SETTLED) | No match | ANALYZING → UNKNOWN_ITEM |
| Settling timeout | 3s no resolution | ANALYZING → IDLE |
| WEIGHT_CHANGE | `delta < -threshold` | TOOL_PLACED → REMOVING |
| MOTION_DETECTED(SETTLED) | Removal match found | REMOVING → IDLE |
| REMOVING_TIMEOUT | 5s elapsed | REMOVING → IDLE |
| CALIBRATION message | baseline set | any → IDLE |

### Door Close Re-evaluation

When reed switch transitions open→closed, after 500ms stable weight:
- Weight near baseline → clear all contents (box is empty)
- Weight matches tools → sync contents
- Transitions to IDLE

This handles manual rearrangement without triggering the normal flow.

## 2. AccessController — Access State Machine

Manages fingerprint scanning, server auth, and door unlock.

```
IDLE ──► SCANNING ──► LOCAL_AUTH_CHECK ──► CHECKING_SERVER
  ▲        │               │                    │
  │        │     ┌─────────┘                    │
  │        │     ▼                              ▼
  │        │  GRANTED ──► UNLOCKING ──► UNLOCKED
  │        │     │
  │        └──► DENIED (2s display) ────────────┘
  │             │
  │             └── ENROLLING ───────────────────┘
  │
  └── (door closed + locked)
```

### Auth Flow (local-first — current implementation)

```
SCANNING (fp match) → LOCAL_AUTH_CHECK
  ├── user found + active → GRANTED
  └── no match → CHECKING_SERVER
        ├── server: allow → GRANTED
        ├── server: deny → DENIED
        └── server unreachable
              ├── localFallback enabled → LOCAL_AUTH_CHECK (retry)
              └── localFallback disabled → DENIED
```

### Enrollment Flow

```
IDLE → ENROLLING
  ├── step=-2 → failed → IDLE
  ├── step=-1 → timeout → IDLE
  ├── step=0  → waiting for first fingerprint scan
  ├── step=1  → waiting for second scan (confirmation)
  └── step=2  → complete → IDLE
```

## 3. DoorService — Door/Lock State Machine

```
LOCKED ──► UNLOCKING ──► UNLOCKED ──► LOCKING ──► LOCKED
               │                           │
               └────── HELD_OPEN (30s) ────┘
```

| State | Condition |
|-------|-----------|
| LOCKED | Relay de-energized, door locked |
| UNLOCKING | Relay energized, 100ms debounce |
| UNLOCKED | Door should be open, auto-lock timer running |
| HELD_OPEN | Door open > 30s (warning event) |

### Auto-Lock
Relay energized → timer starts. After `lockDurationMs` (default 5000ms):
relay de-energized → state = LOCKED.

### Reed Switch
3-sample debounce, 10ms between samples. LOW = door closed, HIGH = open.
Events: DOOR_OPEN, DOOR_CLOSE, DOOR_HELD_OPEN.

## 4. PowerManager — Sleep State Machine

```
PM_ACTIVE
  ├── idle > lightSleepThreshold (10s default)
  │     → PM_LIGHT_SLEEP
  │     → esp_light_sleep_start()
  │     → wake: MPU INT (GPIO35) or timer
  │     → PM_ACTIVE
  │
  └── idle > deepSleepThreshold (60s default)
        → PM_DEEP_SLEEP
        → save baseline to RTC memory
        → esp_deep_sleep_start()
        → wake: timer only → cold boot
```

Activity sources: WeightService (ACTIVITY msg), MotionService (MOTION_WAKE msg).
Idle timer reset on each activity event.

# Citations

[1] src/domain/services/StateManager.cpp — Box state machine
[2] src/domain/services/AccessController.cpp — Access state machine
[3] src/domain/services/DoorService.cpp — Door state machine
[4] src/kernel/PowerManager.h — Power states
