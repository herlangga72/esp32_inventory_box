---
type: Data Layer
title: Persistence Architecture
description: StorageManager (NVS wrapper), entity serialization (pipe-delimited), SPIFFS-backed circular log buffer, lazy-loaded repo cache pattern.
tags: [data, nvs, persistence, spiffs, serialization]
timestamp: 2026-06-29T00:00:00Z
---

# Data Layer

## StorageManager (NVS)

Wraps ESP32 Preferences library. namespace: `"inventory_box"`. Internally heap-allocates.

```cpp
class StorageManager {
    bool begin();
    String getString(const char* key, const char* def);
    void putString(const char* key, const char* value);
    float getFloat(const char* key, float def);
    void putFloat(const char* key, float value);
    int getInt(const char* key, int def);
    void putInt(const char* key, int value);
    bool remove(const char* key);
    void clear();    // factory reset
};
```

### NVS Keys

| Key | Type | Purpose |
|-----|------|---------|
| `wifi_ssid` / `wifi_pass` | string | STA credentials |
| `baseline` | float | Saved weight baseline |
| `tool_next_id` | int | Auto-increment ID |
| `tool_count` | int | Total tool count |
| `tool_N` | string | Tool data (pipe-delimited) |
| `user_next_id` / `user_count` | int | User ID tracking |
| `user_N` | string | User data |
| `cfg_server_url` / `cfg_server_token` | string | Server config |
| `cfg_access_local_fallback` | string | "true"/"false" |
| `cfg_threshold` | float | Weight threshold |
| `cfg_settling` | int | Settling time |
| `cfg_motion` | float | Motion threshold |
| `cfg_light` / `cfg_deep` | int | Sleep timeouts |
| `cfg_tolerance` | float | Default match tolerance |
| `cfg_maxcontents` | int | Max box contents |

## Serialization Format

Pipe-delimited strings in NVS:

### Tool
```
id|name|weightGrams|toleranceGrams|active|createdAt|updatedAt
1|Hammer|450.0|5.0|1|1718236800|1718236800
```

### User
```
id|name|pin|active|createdAt|fpId|totalUsageSeconds|sessionCount|toolPlacements|toolRemovals
1|Alice|1234|1|1718236800|5|3600|42|10|8
```

JSON fallback supported in deserializer (parses `{"n":"name","id":1,...}`).

## Repositories — Cache Pattern

All repos follow lazy-loaded cache pattern in data pool:

```
tr_init():             mem->cacheValid = false
tr_findAll():          if !cacheValid → loadCache() → iterate tool_0..tool_N → deserialize
tr_create/update:      mutate NVS + set cacheValid = false
```

[ToolRepository](/kb/data/repositories.md) — cache[20], Tool entity 56 bytes
[UserRepository](/kb/data/repositories.md) — cache[50], User entity 72 bytes

## LogRepository (SPIFFS)

```cpp
class LogRepository {
    int count();
    int findFiltered(LogEntry* buf, int bufSize, int limit, int offset, int level);
    String downloadCSV();
    void clear();
    int getDropped();
    size_t fileSize();
};
```

- Circular buffer in SPIFFS file, max 500 entries
- Oldest entries overwritten when full
- `droppedCount` tracks overwrites
- CSV export via `GET /api/logs/download`
- Thread safety via `spiffsLock()`/`spiffsUnlock()`

## LogEntry

```cpp
struct LogEntry {
    unsigned long timestamp;
    int severity;           // 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
    char event[32];         // tag (e.g., "STATE", "ACCESS", "WEIGHT")
    char message[128];
    int userId;
    int toolId;
    float weightGrams;
};
```

## Async LogManager

Non-blocking logger using FreeRTOS queue:

```cpp
void logInit(UBaseType_t priority, uint16_t stackDepth);
void logSetLevel(LogLevel level);   // NONE, ERROR, WARN, INFO, DEBUG
LogLevel logGetLevel();
```

Macros (safe from any task or ISR):
```cpp
LOG_ERROR("TAG", "format %d", val);
LOG_WARN("TAG", "format %d", val);
LOG_INFO("TAG", "format %d", val);
LOG_DEBUG("TAG", "format %d", val);
```

Queue depth: 128. Zero cost when level is disabled (macro-level check).
Dropped tracking: `logGetDropped()`.

# Citations

[1] docs/data-layer.md — Full data documentation
[2] src/data/StorageManager.cpp — NVS wrapper
[3] src/data/LogRepository.cpp — SPIFFS logger
