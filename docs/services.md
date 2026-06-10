# Domain Services

All domain services use **free functions on typed memory structs** in `domainPool`. No classes, no virtual dispatch, no heap.

## WeightService

**Memory**: `WeightServiceMemory` — calibration factor, moving average filter (10 samples), baseline, current/previous weight, calibration state.

**Functions**:
```cpp
void ws_onRawReading(WeightServiceMemory* mem, int32_t raw);
void ws_update(WeightServiceMemory* mem);         // drains mailbox
float ws_getCurrentWeight(const WeightServiceMemory* mem);
float ws_getBaseline(const WeightServiceMemory* mem);
float ws_getDelta(const WeightServiceMemory* mem);
```

**Algorithm**:
1. `ws_onRawReading()`: apply moving average filter → `currentWeight`
2. If calibrating: accumulate samples. When done → set baseline, publish event.
3. If not calibrating: `processWeight()` → send WEIGHT_CHANGE to StateManager (delta + currentWeight) + WEIGHT_UPDATE to DisplayManager + publish to EventBus.

**Mailbox commands**: SET_BASELINE, START_CALIBRATION (with sample count in u4.u1), TARE.

**Calibration mode**: caller sends START_CALIBRATION with N samples. Each raw reading goes to accumulator instead of normal processing. When samples exhausted → baseline = sum/N → CALIBRATION_COMPLETE event.

## MotionService

**Memory**: `MotionServiceMemory` — resting accel[3], current accel[3], motion type, initialized flag.

**Functions**:
```cpp
void ms_update(MotionServiceMemory* mem, MPU6050Driver* mpu);
```

**Algorithm** (called at 100 Hz AP / 10 Hz STA):
1. Read accelerometer from MPU6050
2. Compute deltas from resting baseline (captured at boot)
3. Classify motion:
   - `totalMag < 0.16g` → FREE_FALL
   - `zDelta > 0.7g` → TILT (lid open)
   - `magnitude > 0.5g` → MOVEMENT
   - `magnitude > 0.15g` → VIBRATION
   - else → SETTLED
4. On type change → send MOTION_DETECTED to StateManager (`bytes.b0 = MotionType`) + publish to EventBus.

**Motion significance**:
- SETTLED during ANALYZING state → triggers tool matching
- TILT / FREE_FALL → logged as "LID_OPEN"
- All motion types → sent to PowerManager as MOTION_WAKE

## MatchingService

**Memory**: None (stateless — doesn't use registry pool).

**Functions**:
```cpp
int ms_matchByWeight(const Tool* tools, int toolCount, float delta, float tolerance,
                     int* outIds, int maxResults);
int ms_matchClosest(const Tool* tools, int toolCount, float delta,
                    int* outId, float* outConfidence);
```

**Algorithm**:
- `ms_matchByWeight`: iterate active tools. `abs(delta - tool.weightGrams) <= tool.toleranceGrams` → match. Returns count, writes IDs to `outIds[]`.
- `ms_matchClosest`: find smallest `abs(delta - weightGrams)`. Confidence = `1.0 - (bestDiff / bestTolerance)`, clamped [0,1].

## StateManager

**Memory**: `StateManagerMemory` — baseline, current/previous weight, contents[10], currentUserId, session timer, match results, state + stateStartMs.

**Functions**:
```cpp
void sm_init(StateManagerMemory* mem, StorageManager* storage);
void sm_dispatchMessage(StateManagerMemory* mem, const ServiceMessage& msg);
void sm_updatePeriodic(StateManagerMemory* mem);
// Queries
int  sm_getCurrentState(const StateManagerMemory* mem);
int  sm_getCurrentUserId(const StateManagerMemory* mem);
const int32_t* sm_getContents(const StateManagerMemory* mem);
int  sm_getContentCount(const StateManagerMemory* mem);
```

### State Machine

```
INIT(0) → IDLE(1) → ANALYZING(2) → TOOL_PLACED(3) → REMOVING(4) → IDLE
                       ↓                                     ↑
                   UNKNOWN_ITEM(5) ─────────────────────────┘
                       ↑
              CALIBRATING(6) → IDLE
              ERROR(7) (unused)
              SLEEP(8) → IDLE
```

**Valid transitions enforced** — `isValidTransition(from, to)` checks before any state change.

### Message Handlers

| Message | Payload | Action |
|---------|---------|--------|
| WEIGHT_CHANGE | f2.f1=delta, f2.f2=currentWeight | IDLE+delta>threshold → ANALYZING. ANALYZING → reset settling timer. TOOL_PLACED+delta< -threshold → REMOVING |
| MOTION_DETECTED | bytes.b0=MotionType | SETTLED+ANALYZING → run MatchingService → TOOL_PLACED or UNKNOWN_ITEM. TILT/FREE_FALL → log LID_OPEN |
| TOOL_MATCHED | u2.u1=toolId | Add to contents, TOOL_PLACED |
| UNKNOWN_WEIGHT | f2.f1=weight | UNKNOWN_ITEM |
| USER_LOGIN | u2.u1=userId | Set currentUserId, sessionStartMs |
| USER_LOGOUT | none | Clear currentUserId, sessionStartMs |
| CALIBRATION | f2.f1=baseline | Store baseline, forward SET_BASELINE to WeightService |
| ENTER_SLEEP | none | SLEEP state |
| WAKE | none | IDLE state |

### Periodic Update
`sm_updatePeriodic()`: if ANALYZING and settling time (3s) elapsed → IDLE (weight stabilized but no tool match — likely just drift/noise).

### Contents Management
`contents[10]` = tool IDs currently in box. `addToContents()` skips duplicates. `removeFromContents()` shifts array. Web API reads via `sm_getContents()`.

### Dual Publish
Every state change publishes to BOTH EventBus (legacy) AND DisplayManager mailbox (new path). Ensures backward compatibility.

## AccessController

**Memory**: `AccessControllerMemory` — state, lastFpId, enrollment state, local fallback flag, current unlock user info, last event text.

**State machine** (10 states):
```
IDLE → SCANNING → CHECKING_SERVER → GRANTED → UNLOCKING → UNLOCKED → LOCKING → IDLE
                       ↓                                    ↑
                  LOCAL_AUTH_CHECK ─────────────────────────┘
                       ↓
                    DENIED → IDLE
IDLE → ENROLLING → IDLE
```

**Functions**:
```cpp
void ac_init(AccessControllerMemory* mem);
void ac_dispatchMessage(AccessControllerMemory* mem, const ServiceMessage& msg,
                        FingerprintDriver* fp, ServerClient* sc, DoorServiceMemory* ds);
void ac_update(AccessControllerMemory* mem, FingerprintDriver* fp,
               ServerClient* sc, DoorServiceMemory* ds);
// Commands
bool ac_beginEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId);
bool ac_cancelEnrollment(AccessControllerMemory* mem, FingerprintDriver* fp);
bool ac_deleteFingerprint(AccessControllerMemory* mem, FingerprintDriver* fp, int fpId);
bool ac_remoteUnlock(AccessControllerMemory* mem, DoorServiceMemory* ds);
```

### Detailed Flow

1. **IDLE**: call `fp->startScan()` → transition to SCANNING
2. **SCANNING**: poll `fp->checkScan()`. On match (result >= 0) → if server configured → CHECKING_SERVER, else → LOCAL_AUTH_CHECK
3. **CHECKING_SERVER**: `sc->checkAccess(fpId)` → HTTP POST to remote server
   - result=1 (allow) → GRANTED
   - result=0 (deny) → DENIED
   - result=-1 (error) → LOCAL_AUTH_CHECK (if fallback enabled) or DENIED
4. **LOCAL_AUTH_CHECK**: `ur_findByFingerprintId(fpId)`. Found+active → GRANTED. Else → DENIED
5. **GRANTED**: send USER_LOGIN to StateManager, send UNLOCK to DoorService → UNLOCKING
6. **UNLOCKING**: wait for DoorService state to become UNLOCKED → UNLOCKED
7. **UNLOCKED**: wait for door to close + relay auto-lock → IDLE
8. **DENIED**: 2s display timer → IDLE
9. **ENROLLING**: poll `fp->checkEnrollStep()`. step=2 → success. step=-2 → fail. step=-1 → timeout. step=0/1 → wait.

**Local fallback**: enabled by default. Controlled via `cfg_access_local_fallback` in NVS. Auto-disabled if server is reachable and explicitly configured to deny fallback.

## DoorService

**Memory**: `DoorServiceMemory` — state, relayActivateTime, lockDurationMs, reed switch state.

**State machine**:
```
LOCKED → UNLOCKING → UNLOCKED → LOCKING → LOCKED
                      ↓
                  HELD_OPEN (reed open > 30s)
```

**Functions**:
```cpp
void ds_begin(DoorServiceMemory* mem);
void ds_dispatchMessage(DoorServiceMemory* mem, const ServiceMessage& msg);
void ds_update(DoorServiceMemory* mem);
bool ds_isDoorOpen(const DoorServiceMemory* mem);
```

**Mailbox commands**:
- UNLOCK: energize relay (active LOW), set 5s auto-lock timer. Optional duration override in u32.u32_1.
- LOCK: de-energize relay immediately.
- SET_DURATION: change lock pulse duration.

**Auto-lock**: timer checks in `ds_update()`. When relay energized > lockDurationMs → de-energize → LOCKED.

**Reed switch**: 3-sample debounce (`ds_readReedDebounced()`). HIGH = door open. On change → DOOR_OPEN/DOOR_CLOSE events. Open > 30s → HELD_OPEN warning.

**Safe-fail**: relay active state = LOW (energized). Default state = HIGH (de-energized = locked). Power loss = door stays locked.
