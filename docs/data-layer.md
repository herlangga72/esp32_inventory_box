# Data Layer

## StorageManager (NVS Wrapper)

Wraps ESP32 Preferences library. Internally heap-allocates (unavoidable — Preferences uses heap).

```cpp
class StorageManager {
    bool begin();                              // namespace: "inventory_box"
    String getString(const char* key, const char* def);
    void putString(const char* key, const char* value);
    float getFloat(const char* key, float def);
    void putFloat(const char* key, float value);
    int getInt(const char* key, int def);
    void putInt(const char* key, int value);
    bool remove(const char* key);
    void clear();
};
```

### NVS Keys Used

| Key | Type | Purpose |
|-----|------|---------|
| `wifi_ssid` | string | WiFi STA credentials |
| `wifi_pass` | string | WiFi STA password |
| `baseline` | float | Saved weight baseline (survives reboot) |
| `tool_next_id` | int | Auto-increment tool ID |
| `tool_count` | int | Total tool count |
| `tool_N` | string | Tool entity (pipe-delimited) |
| `user_next_id` | int | Auto-increment user ID |
| `user_count` | int | Total user count |
| `user_N` | string | User entity (pipe-delimited) |
| `cfg_server_url` | string | Access server URL |
| `cfg_server_token` | string | Access server auth token |
| `cfg_access_local_fallback` | string | "true"/"false" |
| `cfg_threshold` | float | Weight threshold override |
| `cfg_settling` | int | Settling time override |
| `cfg_motion` | float | Motion threshold override |
| `cfg_light` | int | Light sleep timeout |
| `cfg_deep` | int | Deep sleep timeout |
| `cfg_tolerance` | float | Default tolerance |
| `cfg_maxcontents` | int | Max box contents |

## ToolRepository

**Memory**: `ToolRepositoryMemory` — `Tool cache[20]`, count, cacheValid flag.

**Functions** — all take `ToolRepositoryMemory*` + `StorageManager*`:
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

**Cache pattern**: lazy-loaded from NVS on first access. `cacheValid = false` after any mutation. `loadCache()` iterates keys `tool_0` through `tool_count`, deserializes each.

**Serialization** (pipe-delimited):
```
id|name|weightGrams|toleranceGrams|active|createdAt|updatedAt
```
Example: `1|Hammer|450.0|5.0|1|1718236800|1718236800`

JSON fallback supported in deserializer (parses `{"n":"name","id":1,"w":450.0,...}`).

## UserRepository

**Memory**: `UserRepositoryMemory` — `User cache[50]`, count, cacheValid.

**Functions**:
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

**Authentication**: `ur_authenticate(pin)` — linear scan of cached active users, `strcmp(pin)` match. Returns User* or nullptr.

**Fingerprint lookup**: `ur_findByFingerprintId(fpId)` — linear scan for `user.fpId == fpId && user.active`. Used by AccessController local auth.

**Serialization** (pipe-delimited):
```
id|name|pin|active|createdAt|fpId|totalUsageSeconds|sessionCount|toolPlacements|toolRemovals
```
User stats (usage, sessions, placements, removals) are persisted.

## LogRepository

SPIFFS-backed circular buffer. Max 500 entries (Config::MAX_LOGS).

```cpp
class LogRepository {
    int count();
    std::vector<LogEntry> findAll(int limit, int offset);
    std::vector<LogEntry> findFiltered(int limit, int offset, int level);
    String downloadCSV();
    void clear();
    int getDropped();
    size_t fileSize();
};
```

### LogEntry
```
timestamp | severity | event (tag) | message | userId | toolId | weightGrams
```

**Circular buffer**: oldest entries overwritten when full. `droppedCount` tracks overwrites.

**CSV export**: `GET /api/logs/download` — returns full log as `text/csv`.

**Filtered query**: `findFiltered(limit, offset, level)` — used by Web API. Pagination via limit/offset params.

## Entity Structs

### Tool
```cpp
struct Tool {
    int id;
    char name[32];
    float weightGrams;
    float toleranceGrams;    // default: 5.0 (Config::DEFAULT_TOLERANCE)
    bool active;
    time_t createdAt;
    time_t updatedAt;
};
static_assert(sizeof(Tool) <= 56)
```

### User
```cpp
struct User {
    int id;
    char name[32];
    char pin[5];             // 4-digit + null
    bool active;
    time_t createdAt;
    int fpId;                // 0 = no fingerprint, 1-127 = enrolled slot
    unsigned long totalUsageSeconds;
    int sessionCount;
    int toolPlacements;
    int toolRemovals;
};
static_assert(sizeof(User) <= 72)
```

### LogEntry
```cpp
struct LogEntry {
    unsigned long timestamp;
    int severity;            // 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
    char event[32];          // tag
    char message[128];
    int userId;
    int toolId;
    float weightGrams;
};
```

## LogManager (Async Logger)

Non-blocking logger using FreeRTOS queue + dedicated logger task.

```cpp
void logInit(UBaseType_t priority = 2, uint16_t stackDepth = 2560);
void logSetLevel(LogLevel level);   // NONE, ERROR, WARN, INFO, DEBUG
LogLevel logGetLevel();
```

**Macros** (safe from any task or ISR):
```cpp
LOG_ERROR("TAG", "format %d", val);
LOG_WARN("TAG", "format %d", val);
LOG_INFO("TAG", "format %d", val);
LOG_DEBUG("TAG", "format %d", val);
```

Each macro checks current level before calling `logEnqueue()` — zero cost when disabled.

**LogMsg**: `timestamp | level | tag[8] | message[128]` — pushed to FreeRTOS queue. Logger task dequeues and writes to Serial + SPIFFS LogRepository.

**Dropped tracking**: `logGetDropped()` returns count of messages lost due to full queue. Queue depth: 128 (Config::LOG_QUEUE_DEPTH).
