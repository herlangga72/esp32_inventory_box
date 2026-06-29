---
type: Data Layer
title: Repository Functions
description: Free-function repository API for Tool, User, and Log persistence with lazy-loaded cache pattern.
tags: [repositories, cache, crud, nvs, spiffs]
timestamp: 2026-06-29T00:00:00Z
---

# Repository Functions

All repos use **free functions on `*Memory` structs** in dataPool. Same pattern as domain services.

## ToolRepository

```cpp
void tr_init(ToolRepositoryMemory* mem, StorageManager* storage);
int  tr_create(ToolRepositoryMemory* mem, StorageManager* storage, Tool* tool);
bool tr_update(ToolRepositoryMemory* mem, StorageManager* storage, int id, Tool* tool);
bool tr_remove(ToolRepositoryMemory* mem, StorageManager* storage, int id);
Tool* tr_findById(ToolRepositoryMemory* mem, StorageManager* storage, int id);
int  tr_findAll(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools);
int  tr_findActive(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools);
int  tr_count(ToolRepositoryMemory* mem, StorageManager* storage);
```

Capacity: 20 tools (Config::MAX_TOOLS). NVS keys: `tool_0` through `tool_N`.

Cache pattern:
```cpp
tr_findAll():
    if (!mem->cacheValid) loadCache();   // iterate "tool_0".."tool_N"
    memcpy(outBuf, mem->cache, ...);

tr_create():
    int id = storage.getInt("tool_next_id", 1);
    tool->id = id;
    storage.putString("tool_N", tool.serialize());
    storage.putInt("tool_next_id", id + 1);
    storage.putInt("tool_count", count + 1);
    mem->cacheValid = false;
```

## UserRepository

```cpp
void ur_init(UserRepositoryMemory* mem, StorageManager* storage);
int  ur_create(UserRepositoryMemory* mem, StorageManager* storage, User* user);
bool ur_update(UserRepositoryMemory* mem, StorageManager* storage, int id, User* user);
bool ur_remove(UserRepositoryMemory* mem, StorageManager* storage, int id);
User* ur_findById(UserRepositoryMemory* mem, StorageManager* storage, int id);
int  ur_findAll(UserRepositoryMemory* mem, StorageManager* storage, User* outBuf, int maxUsers);
int  ur_count(UserRepositoryMemory* mem, StorageManager* storage);
User* ur_authenticate(UserRepositoryMemory* mem, StorageManager* storage, const char* pin);
User* ur_findByFingerprintId(UserRepositoryMemory* mem, StorageManager* storage, int fpId);
```

Capacity: 50 users (Config::MAX_USERS).

### Authentication

```cpp
User* ur_authenticate(urMem, storage, pin):
    if (!cacheValid) loadCache();
    for (int i = 0; i < count; i++)
        if (cache[i].active && strcmp(cache[i].pin, pin) == 0)
            return &cache[i];
    return nullptr;
```

### Fingerprint Lookup

```cpp
User* ur_findByFingerprintId(urMem, storage, fpId):
    if (!cacheValid) loadCache();
    for (int i = 0; i < count; i++)
        if (cache[i].fpId == fpId && cache[i].active)
            return &cache[i];
    return nullptr;
```

Used by AccessController for local auth fallback.

## LogRepository

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

SPIFFS-backed circular buffer:
- Max 500 entries (Config::MAX_LOGS)
- Oldest entries overwritten when full
- `droppedCount` tracks overwrites
- Thread safety: `spiffsLock()`/`spiffsUnlock()` around all operations

# Citations

[1] src/data/ToolRepository.cpp — Tool CRUD
[2] src/data/UserRepository.cpp — User CRUD + auth
[3] src/data/LogRepository.cpp — Log management
