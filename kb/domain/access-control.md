---
type: Domain Logic
title: Access Control Subsystem
description: Fingerprint authentication (R307), local user lookup, remote server auth, enrollment lifecycle, and door unlock coordination.
tags: [access-control, fingerprint, authentication, enrollment]
timestamp: 2026-06-29T00:00:00Z
---

# Access Control Subsystem

Four components working together:

1. **FingerprintDriver** — HAL wrapper for R307/AS608 over UART2
2. **AccessController** — State machine orchestrating auth flow
3. **DoorService** — Relay + reed switch control
4. **ServerClient** — HTTP client to remote access server

## FingerprintDriver

Wraps Adafruit_Fingerprint library. UART2 remapped to pins 5 (RX) / 4 (TX), 57600 baud.

```cpp
class FingerprintDriver {
    bool begin();                    // UART init, sensor handshake, password verify
    void startScan();                // Arm sensor (non-blocking poll)
    int  checkScan();                // -1=no finger, -2=no match, >=0=fpId
    bool startEnroll(int id);        // Begin multi-step enrollment
    int  checkEnrollStep();          // -2=fail, -1=timeout, 0=need img1, 1=need img2, 2=success
    void cancelEnroll();             // Abort enrollment
    bool deleteFingerprint(int id);  // Delete single template
    bool deleteAll();                // Clear all templates
    int  getTemplateCount();         // Stored templates count
};
```

Capacity: 128 templates. Scan interval: 200ms. Enrollment timeout: 30s.

## Auth Flow (Local-First)

Current implementation checks local users BEFORE remote server:

```
IDLE → fp.startScan() → SCANNING
  │
  ▼ (fpId matched)
LOCAL_AUTH_CHECK
  ├── ur_findByFingerprintId(fpId) found + active → GRANTED
  └── not found → CHECKING_SERVER
        ├── sc->checkAccess(fpId) == 1 (allow) → GRANTED
        ├── sc->checkAccess(fpId) == 0 (deny) → DENIED
        └── sc->checkAccess(fpId) == -1 (unreachable)
              ├── localFallback enabled → LOCAL_AUTH_CHECK (retry)
              └── localFallback disabled → DENIED
```

### After GRANTED
1. Send USER_LOGIN(foundUserId) to StateManager
2. Send UNLOCK to DoorService
3. UNLOCKING → wait for DoorService state → UNLOCKED
4. Wait for door close + relay auto-lock → IDLE

### DENIED
2 second display timer, then return to IDLE.

## Enrollment Flow

```
POST /api/fingerprint/enroll {"fpId": 10}
  → ac_beginEnrollment(fpId)
    → fp->startEnroll(fpId)
    → state = ENROLLING
    → return success

GET /api/fingerprint/enroll/status (poll)
  → fp->checkEnrollStep()
    step=-2: FAILED → IDLE
    step=-1: TIMEOUT (30s) → IDLE
    step=0:  waiting for first finger scan
    step=1:  first OK, waiting for second scan (confirmation)
    step=2:  SUCCESS → IDLE
```

## Remote Unlock (API override)

```
POST /api/door/unlock
  → ac_remoteUnlock()
    → bypasses fingerprint, goes directly to GRANTED
    → sends UNLOCK to DoorService
    → logs "remote unlock via API"
```

## ServerClient

HTTP client for remote access server auth + health checks.

```cpp
class ServerClient {
    void begin(const char* url, const char* token);
    int  checkAccess(int fpId);       // POST → -1/0/1
    void update();                     // Heartbeat + retry state machine
    bool isConfigured();
    bool isServerReachable();
    uint32_t getLastResponseTime();
    uint32_t getServerFailDuration();
};
```

- `checkAccess(fpId)` returns -1 (error/unreachable), 0 (deny), 1 (allow)
- Heartbeat health check interval tracks server reachability
- Config stored in NVS (`cfg_server_url`, `cfg_server_token`)

## Local Fallback

Enabled by default. Controlled via NVS key `cfg_access_local_fallback`.
When server is unreachable, system falls back to local user repository auth.
Can be disabled if server-only auth is required.

## DoorService

See [State Machines](/kb/domain/state-machines.md) §3 for full door state machine.

- Relay: pin 13, active LOW, 5s pulse
- Reed switch: pin 14, INPUT_PULLUP, LOW=closed
- Auto-lock timer de-energizes relay after `lockDurationMs`
- HELD_OPEN warning at 30s

## NVS Keys

| Key | Type | Purpose |
|-----|------|---------|
| `cfg_server_url` | string | Remote access server URL |
| `cfg_server_token` | string | Auth token for server |
| `cfg_access_local_fallback` | string | "true"/"false" |

# Citations

[1] src/domain/services/AccessController.cpp — Full implementation
[2] src/domain/services/DoorService.cpp — Door/lock control
[3] src/kernel/ServerClient.cpp — HTTP client
[4] src/hal/FingerprintDriver.cpp — Sensor driver
