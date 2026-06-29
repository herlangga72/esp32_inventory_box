---
type: Data Layer
title: Entity Definitions
description: Tool, User, and LogEntry struct definitions with field constraints and serialization format.
tags: [entities, structs, data, serialization]
timestamp: 2026-06-29T00:00:00Z
---

# Entity Definitions

## Tool

```cpp
struct Tool {
    int id;
    char name[32];           // max 31 chars + null
    float weightGrams;        // expected weight when placed
    float toleranceGrams;     // ±grams for matching (default 5.0)
    bool active;              // soft delete / enable flag
    time_t createdAt;
    time_t updatedAt;
};
static_assert(sizeof(Tool) <= 56);
```

### Serialization
```
id|name|weightGrams|toleranceGrams|active|createdAt|updatedAt
1|Hammer|450.0|5.0|1|1718236800|1718236800
```

## User

```cpp
struct User {
    int id;
    char name[32];           // display name
    char pin[5];             // 4-digit PIN + null terminator
    bool active;
    time_t createdAt;
    int fpId;                // 0 = no fingerprint, 1-127 = enrolled slot
    unsigned long totalUsageSeconds;
    int sessionCount;
    int toolPlacements;
    int toolRemovals;
};
static_assert(sizeof(User) <= 72);
```

Stats (totalUsageSeconds, sessionCount, etc.) are persisted but not actively updated in current code.

### Serialization
```
id|name|pin|active|createdAt|fpId|totalUsageSeconds|sessionCount|toolPlacements|toolRemovals
1|Alice|1234|1|1718236800|5|3600|42|10|8
```

## LogEntry

```cpp
struct LogEntry {
    unsigned long timestamp;
    int severity;            // 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
    char event[32];          // tag/category
    char message[128];       // formatted log text
    int userId;              // associated user (0 if none)
    int toolId;              // associated tool (-1 if none)
    float weightGrams;       // weight at time of event
};
```

## LogMsg (Internal Logger Queue)

```cpp
struct LogMsg {
    unsigned long timestamp;
    uint8_t level;
    char tag[8];
    char message[128];
};
```

Pushed to FreeRTOS queue, drained by logger task to Serial + SPIFFS.

## BoxState (Legacy)

```cpp
struct BoxState {
    bool isOpen;
    float currentWeight;
    int userId;
    int toolCount;
    uint32_t lastActivity;
};
```

Currently unused — state lives in StateManagerMemory.

## Repository Cache Memory

```cpp
struct ToolRepositoryMemory {
    Tool cache[Config::MAX_TOOLS];   // 20 tools
    int32_t count;
    bool cacheValid;
};

struct UserRepositoryMemory {
    User cache[Config::MAX_USERS];   // 50 users
    int32_t count;
    bool cacheValid;
};
```

# Citations

[1] src/domain/entities/Tool.h — Tool struct
[2] src/domain/entities/User.h — User struct
[3] src/domain/entities/LogEntry.h — LogEntry struct
[4] src/domain/entities/BoxState.h — BoxState struct
