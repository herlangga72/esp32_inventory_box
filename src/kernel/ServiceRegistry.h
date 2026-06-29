// ======================================================================
// ServiceRegistry.h — Microkernel Service Registry
// ======================================================================
// Single file-scope static instance (g_registry) at fixed link-time addr.
// Every service's state lives at a known compile-time offset from &g_registry.
// No heap allocation after boot. No STL. No virtual functions.
// ======================================================================
#ifndef SERVICE_REGISTRY_H
#define SERVICE_REGISTRY_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "../domain/entities/Tool.h"
#include "../domain/entities/User.h"
#include "../domain/entities/LogEntry.h"
#include "../domain/entities/BoxState.h"
#include "../config/Config.h"

// ======================================================================
// CONSTANTS
// ======================================================================
#define SR_MAGIC        0x53455256  // "SERV"
#define SR_VERSION      1
#define SR_MAX_SERVICES 22

// ======================================================================
// MODULE MEMORY MAP — every subsystem has a fixed compile-time arena.
// Total: ~106KB static. Target: zero malloc/free after boot.
// ======================================================================

// ---- Service state pools (existing) ----
#define SR_POOL_KERNEL_SIZE         16384   // Logger, Storage, WiFi, Power, SystemStatus state structs
#define SR_POOL_HAL_SIZE            2048    // HX711, MPU6050, SSD1306, Fingerprint driver state
#define SR_POOL_DOMAIN_SIZE         1024    // WeightService, MotionService, StateManager, AccessCtrl, Door
#define SR_POOL_DATA_SIZE           8192    // ToolRepo (20×56=1120), UserRepo (50×72=3600), LogRepo cache
#define SR_POOL_PRESENTATION_SIZE   1024    // WebServer, DisplayManager, SerialCLI, ServerClient state

// ---- IPC infrastructure ----
#define SR_POOL_QUEUE_BUFFERS_SIZE  4096    // 7 mailboxes × static queue data buffers
#define SR_TASK_STACK_POOL_SIZE     16384   // 6 domain tasks (State 4K, Access 3K, Weight 3K, Motion 1.5K, Web 2.5K, Display 2K)

// ---- Module arenas (NEW — replaces heap for each subsystem) ----
#define SR_WIFI_POOL_SIZE           4096    // [existing] WiFi OSI allocator (lwIP internal buffers)
#define SR_NET_ARENA_SIZE           1536    // [new] WebServer sockets + HTTP client buffers
#define SR_LOGGER_ARENA_SIZE        1536    // [new] Logger queue + task stack (converted from heap)
#define SR_FS_ARENA_SIZE            1024    // [new] SPIFFS temp workspace (file handles still heap for now)
#define SR_IO_ARENA_SIZE            1024    // [new] I2C + UART driver scratch buffers
#define SR_STRING_ARENA_SIZE        512     // [new] Temp String ring buffer
#define SR_FP_ARENA_SIZE            512     // [new] Adafruit_Fingerprint extra buffer
#define SR_SYS_ARENA_SIZE           512     // [new] FreeRTOS mutex/semaphore prealloc

// Total new arenas: ~6.5KB added to g_registry

// ======================================================================
// SERVICE ID
// ======================================================================
enum class ServiceId : uint8_t {
    NONE = 0,
    LOGGER = 1, STORAGE = 2, WIFI = 3, POWER = 4, SYSTEM_STATUS = 5,
    STATE_MANAGER = 6, WEIGHT_SERVICE = 7, MOTION_SERVICE = 8,
    ACCESS_CONTROLLER = 9, DOOR_SERVICE = 10, MATCHING_SERVICE = 11,
    TOOL_REPOSITORY = 12, USER_REPOSITORY = 13, LOG_REPOSITORY = 14,
    WEB_SERVER = 15, DISPLAY_MANAGER = 16, SERIAL_CLI = 17, SERVER_CLIENT = 18,
    HX711 = 19, MPU6050 = 20, SSD1306 = 21, FINGERPRINT = 22,
    COUNT
};

// ======================================================================
// SERVICE STATE
// ======================================================================
enum class ServiceState : uint8_t {
    UNINIT = 0, INIT = 1, RUNNING = 2, ERROR = 3, STOPPED = 4
};

// SCB flags
enum : uint16_t {
    SR_FLAG_HAS_INBOX  = 0x01,
    SR_FLAG_HAS_TASK   = 0x02,
    SR_FLAG_IS_PASSIVE = 0x04,
    SR_FLAG_HEARTBEAT  = 0x08,
};

// ======================================================================
// SERVICE CONTROL BLOCK — 36 bytes
// ======================================================================
struct ServiceControlBlock {
    uint8_t       id;
    uint8_t       state;          // ServiceState
    uint16_t      flags;
    QueueHandle_t inbox;
    uint32_t      msg_received;
    uint32_t      msg_sent;
    uint32_t      msg_dropped;
    uint32_t      last_heartbeat_ms;
    TaskHandle_t  task;
    uint32_t      owned_memory_offset;
    uint16_t      owned_memory_size;
    uint16_t      reserved;
};
static_assert(sizeof(ServiceControlBlock) == 36, "SCB must be 36 bytes");

// ======================================================================
// SERVICE MESSAGE — 16 bytes with reply routing
// ======================================================================
struct ServiceMessage {
    uint8_t target;
    uint8_t type;
    uint8_t replyTo;
    uint8_t corrId;

    union {
        struct { float    f1; float    f2; }                      f2;
        struct { float    f1; uint16_t u1; uint16_t u2; }         f1u2;
        struct { uint32_t u32_1; uint32_t u32_2; }                u32;
        struct { uint16_t u1; uint16_t u2; }                      u2;
        struct { uint16_t u1; uint16_t u2; uint16_t u3; uint16_t u4; } u4;
        struct {
            uint8_t b0, b1, b2, b3, b4, b5, b6, b7;
            uint8_t b8, b9, ba, bb;
        } bytes;
        uint8_t raw[12];
    };

    ServiceMessage() : target(0), type(0), replyTo(0), corrId(0) {
        memset(raw, 0, sizeof(raw));
    }

    static ServiceMessage cmd(ServiceId tgt, uint8_t msgType) {
        ServiceMessage m;
        m.target = static_cast<uint8_t>(tgt);
        m.type   = msgType;
        return m;
    }

    static ServiceMessage query(ServiceId tgt, ServiceId replyTo,
                                uint8_t msgType, uint8_t corr) {
        ServiceMessage m;
        m.target  = static_cast<uint8_t>(tgt);
        m.type    = msgType;
        m.replyTo = static_cast<uint8_t>(replyTo);
        m.corrId  = corr;
        return m;
    }
};
static_assert(sizeof(ServiceMessage) == 16, "ServiceMessage must be 16 bytes");

// ======================================================================
// PER-SERVICE MESSAGE TYPE ENUMS
// ======================================================================

enum class KernelMsgType : uint8_t {
    PING = 0, ACTIVITY = 1, MOTION_WAKE = 2, ENTER_SLEEP = 3,
    SET_BASELINE = 4, SET_OPERATIONAL_MODE = 5,
    STORAGE_GET = 10, STORAGE_PUT = 11, STORAGE_REMOVE = 12,
    WIFI_STATUS_QUERY = 20, STATUS_QUERY = 21,
};

enum class StateMsgType : uint8_t {
    WEIGHT_CHANGE = 1, MOTION_DETECTED = 2, TOOL_MATCHED = 3,
    UNKNOWN_WEIGHT = 4, USER_LOGIN = 5, USER_LOGOUT = 6,
    CALIBRATION = 7, ENTER_SLEEP = 8, WAKE = 9,
};

enum class WeightMsgType : uint8_t {
    SET_BASELINE = 1, START_CALIBRATION = 2, TARE = 3,
    QUERY_WEIGHT = 10, QUERY_BASELINE = 11,
};

enum class MotionMsgType : uint8_t {
    QUERY_MOTION_STATE = 1, QUERY_ACCEL = 2,
};

enum class AccessMsgType : uint8_t {
    BEGIN_ENROLLMENT = 1, CANCEL_ENROLLMENT = 2, DELETE_FINGERPRINT = 3,
    DELETE_ALL_FP = 4, REMOTE_UNLOCK = 5,
};

enum class DoorMsgType : uint8_t {
    UNLOCK = 1, LOCK = 2, SET_DURATION = 3,
};

enum class DisplayMsgType : uint8_t {
    STATE_CHANGED = 1, TOOL_PLACED = 2, TOOL_REMOVED = 3,
    WEIGHT_UPDATE = 4, NOTIFICATION = 5, USER_LOGIN = 6,
    USER_LOGOUT = 7, SLEEP = 8, WAKE = 9,
};

enum class RepoMsgType : uint8_t {
    FIND_ALL = 1, FIND_BY_ID = 2, CREATE = 3, UPDATE = 4,
    REMOVE = 5, FIND_BY_FP_ID = 6, COUNT = 7,
};

// ======================================================================
// COMPONENT STATUS — fixed size, no heap
// ======================================================================
enum class ComponentStatus : uint8_t { UNKNOWN = 0, OK = 1, WARNING = 2, ERROR = 3 };

struct ComponentInfoFixed {
    char            name[24];
    ComponentStatus status;
    char            lastError[64];
    uint32_t        errorTime;
    uint16_t        errorCount;
    uint8_t         reserved[2];
};
// ComponentInfoFixed: name[24]+status(1+3pad)+lastError[64]+errorTime(4)+errorCount(2)+reserved[2] ≈ 100
static_assert(sizeof(ComponentInfoFixed) <= 128, "ComponentInfoFixed size");

// ======================================================================
// KERNEL SERVICE STATE STRUCTS
// ======================================================================

// Logger
#define LOGGER_BUFFER_ENTRIES 16
struct LoggerMemory {
    LogEntry buffer[LOGGER_BUFFER_ENTRIES];
    uint16_t writeIndex;
    uint16_t readIndex;
    uint32_t droppedCount;
    uint8_t  currentLevel;
    uint8_t  reserved[3];
    QueueHandle_t logQueue;
    TaskHandle_t  loggerTask;
    uint8_t       queueStorage[LOGGER_BUFFER_ENTRIES * sizeof(LogEntry)];
    StaticQueue_t queueStruct;
};

// Storage (NVS wrapper — Preferences internals use heap, unavoidable)
struct StorageMemory {
    char     nvsNamespace[16];
    bool     initialized;
    uint8_t  reserved[3];
};

// WiFi
struct WiFiMemory {
    char     ssid[33];
    char     password[65];
    char     ipAddress[16];
    int8_t   rssi;
    bool     apMode;
    bool     connected;
    uint8_t  reserved[2];
    uint32_t lastStaCheck;
};

// Power
struct PowerMemory {
    uint8_t  currentState;
    bool     sleepAllowed;
    bool     pmConfigured;
    uint8_t  reserved;
    uint32_t lastActivityTime;
    uint32_t lightSleepThreshold;
    uint32_t deepSleepThreshold;
    float    currentBaseline;
    uint8_t  opMode;
    uint8_t  wakeCount;
    uint16_t reserved2;
};

// SystemStatus
#define MAX_COMPONENTS 12
struct SystemStatusMemory {
    ComponentInfoFixed components[MAX_COMPONENTS];
    uint8_t  componentCount;
    bool     bootComplete;
    uint8_t  currentBootStage;
    uint8_t  currentOpMode;
    uint8_t  reserved;
    uint32_t bootStartMs;
    uint16_t totalErrorCount;
    uint16_t reserved2;
    char     lastErrorMsg[128];
};

// ======================================================================
// DOMAIN SERVICE STATE STRUCTS
// ======================================================================

#define WEIGHT_FILTER_SIZE 10
struct WeightServiceMemory {
    float    calibrationFactor;
    float    readings[WEIGHT_FILTER_SIZE];
    uint8_t  filterIndex;
    uint8_t  filterSize;
    uint16_t padding1;
    float    filterSum;
    float    baseline;
    float    currentWeight;
    float    previousWeight;
    uint8_t  calibrating;
    uint8_t  reserved2[3];
    int32_t  calibrationSamples;
    int32_t  totalCalSamples;
    float    calibrationSum;
    uint32_t readingsTaken;
    uint32_t messagesProcessed;
};
static_assert(sizeof(WeightServiceMemory) <= 96, "WeightServiceMemory too large");

struct MotionServiceMemory {
    float    restingAccel[3];
    float    currentAccel[3];
    uint8_t  currentMotion;
    uint8_t  initialized;
    uint8_t  reserved[2];
    uint32_t messagesProcessed;
};
static_assert(sizeof(MotionServiceMemory) <= 32, "MotionServiceMemory too large");

#define MAX_BOX_CONTENTS 10
struct StateManagerMemory {
    float    baselineGrams;
    float    currentWeightGrams;
    float    previousWeightGrams;
    int32_t  contents[MAX_BOX_CONTENTS];
    int32_t  contentCount;
    int32_t  currentUserId;
    uint32_t sessionStartMs;
    uint8_t  state;
    bool     prevDoorOpen;   // for open→closed edge detection in sm_updatePeriodic
    uint8_t  reserved[2];
    uint32_t stateStartMs;
    uint32_t doorClosePendingMs;  // millis() when door close detected, 0 = no pending
    int32_t  matchResults[MAX_BOX_CONTENTS];
    uint8_t  matchCount;
    uint8_t  reserved2[3];
    float    lastMatchConfidence;
    uint8_t  lastCorrId;
    uint8_t  reserved3[3];
    uint32_t messagesProcessed;
};
static_assert(sizeof(StateManagerMemory) <= 160, "StateManagerMemory too large");

struct AccessControllerMemory {
    uint8_t  state;
    uint8_t  reserved1[3];
    uint32_t stateStartMs;
    int32_t  lastFpId;
    char     lastEvent[64];
    uint8_t  enrolling;
    uint8_t  reserved2[3];
    int32_t  enrollingFpId;
    int32_t  enrollStep;
    uint8_t  localFallbackEnabled;
    uint8_t  reserved3[3];
    int32_t  currentUnlockFpId;
    int32_t  currentUnlockUserId;
    char     currentUnlockUserName[32];
    uint32_t messagesProcessed;
};
static_assert(sizeof(AccessControllerMemory) <= 144, "AccessControllerMemory too large");

struct DoorServiceMemory {
    uint8_t  state;
    uint8_t  reserved1[3];
    uint32_t stateStartMs;
    uint32_t relayActivateTime;
    uint32_t lockDurationMs;
    bool     lastReedState;
    uint8_t  reserved2[3];
    uint32_t lastReedChange;
};
static_assert(sizeof(DoorServiceMemory) <= 32, "DoorServiceMemory too large");

// ======================================================================
// DATA SERVICE STATE STRUCTS
// ======================================================================
static_assert(sizeof(Tool) <= 56, "Tool entity too large");
static_assert(sizeof(User) <= 72, "User entity too large");

struct ToolRepositoryMemory {
    Tool     cache[Config::MAX_TOOLS];
    int32_t  count;
    bool     cacheValid;
    uint8_t  reserved2[3];
};

struct UserRepositoryMemory {
    User     cache[Config::MAX_USERS];
    int32_t  count;
    bool     cacheValid;
    uint8_t  reserved2[3];
};

struct LogRepositoryMemory {
    uint32_t cachedLineCount;
    uint32_t cachedFileSize;
    uint32_t lastQueryMs;
    bool     dirty;
    uint8_t  reserved[3];
};

static_assert(sizeof(ToolRepositoryMemory) <= (Config::MAX_TOOLS * 56 + 8), "ToolRepo too large");
static_assert(sizeof(UserRepositoryMemory) <= (Config::MAX_USERS * 72 + 8), "UserRepo too large");

// ======================================================================
// PRESENTATION SERVICE STATE STRUCTS
// ======================================================================
struct WebServerMemory {
    bool     initialized;
    uint8_t  reserved1[3];
    uint32_t requestCount;
    uint32_t lastRequestMs;
    char     apiBuffer[256];
};

enum class DisplayType : uint8_t { SSD1306 = 0, LCD1602 = 1 };

struct DisplayManagerMemory {
    uint8_t  currentScreen;
    bool     healthy;
    bool     awake;
    uint8_t  displayType;    // DisplayType enum — set during auto-detect boot
    uint32_t lastRefreshMs;
    uint32_t lastHealthCheckMs;
    char     notificationText[32];
    uint32_t notificationEndMs;
    float    displayWeight;
    float    displayBaseline;
    float    displayDelta;
    int32_t  displayContentCount;
    char     displayUser[32];
    bool     doorOpen;       // read from DoorService state each LCD cycle
    uint8_t  pad[3];
};
static_assert(sizeof(DisplayManagerMemory) <= 120, "DisplayManagerMemory too large");

struct SerialCLIMemory {
    char     buffer[128];
    uint8_t  pos;
    bool     initialized;
    uint16_t reserved;
};

struct ServerClientMemory {
    char     serverUrl[128];
    char     authToken[64];
    bool     reachable;
    bool     configured;
    uint16_t reserved1;
    char     lastReason[64];
    char     lastUserName[32];
    int32_t  lastUserId;
    uint32_t lastHeartbeatMs;
    uint32_t lastResponseTimeMs;
    uint32_t serverFailStartMs;
    uint32_t heartbeatIntervalMs;
};

// ======================================================================
// HAL DRIVER POOL SIZES
// ======================================================================
#define HX711_POOL_SIZE        64
#define MPU6050_POOL_SIZE      64
#define SSD1306_POOL_SIZE      128
#define LCD1602_POOL_SIZE      64
#define FINGERPRINT_POOL_SIZE  256

// ======================================================================
// SERVICE REGISTRY — root struct at file scope
// ======================================================================
struct ServiceRegistry {
    // Header (16 bytes)
    uint32_t magic;
    uint16_t version;
    uint8_t  serviceCount;
    uint8_t  checksum;
    uint32_t uptimeMs;
    uint16_t bootCount;
    uint16_t padding0;

    // SCB array (36 * SR_MAX_SERVICES bytes)
    ServiceControlBlock scb[SR_MAX_SERVICES];

    // Service memory pools
    uint8_t kernelPool[SR_POOL_KERNEL_SIZE];
    uint8_t halPool[SR_POOL_HAL_SIZE];
    uint8_t domainPool[SR_POOL_DOMAIN_SIZE];
    uint8_t dataPool[SR_POOL_DATA_SIZE];
    uint8_t presentationPool[SR_POOL_PRESENTATION_SIZE];

    // Static queue storage
    uint8_t queueBufferPool[SR_POOL_QUEUE_BUFFERS_SIZE];
    StaticQueue_t queueStructs[SR_MAX_SERVICES];

    // Static WiFi driver pool (for OSI allocator)
    uint8_t wifiPool[SR_WIFI_POOL_SIZE];
    size_t wifiPoolUsed;

    // Static task stacks + TCBs
    uint8_t taskStackPool[SR_TASK_STACK_POOL_SIZE];
    StaticTask_t taskTCBs[6];

    // ---- Module arenas (fixed-size, zero heap after boot) ----
    uint8_t netArena[SR_NET_ARENA_SIZE];          // WebServer sockets, HTTP buffers
    size_t  netArenaUsed;
    uint8_t loggerArena[SR_LOGGER_ARENA_SIZE];    // Logger queue + task stack
    size_t  loggerArenaUsed;
    uint8_t fsArena[SR_FS_ARENA_SIZE];            // SPIFFS file handles, LogFile buffers
    size_t  fsArenaUsed;
    uint8_t ioArena[SR_IO_ARENA_SIZE];            // I2C/UART driver internal buffers
    size_t  ioArenaUsed;
    uint8_t stringArena[SR_STRING_ARENA_SIZE];    // Temp String pool
    size_t  stringArenaUsed;
    uint8_t fpArena[SR_FP_ARENA_SIZE];            // Adafruit_Fingerprint workspace
    size_t  fpArenaUsed;
    uint8_t sysArena[SR_SYS_ARENA_SIZE];          // FreeRTOS mutexes/semaphores
    size_t  sysArenaUsed;

    // ---- Methods ----
    bool init();
    ServiceControlBlock* getSCB(ServiceId id);

    // Messaging
    bool send(ServiceId target, const ServiceMessage& msg);
    bool sendFromISR(ServiceId target, const ServiceMessage& msg,
                     BaseType_t* pxHigherPriorityTaskWoken);
    bool sendCmd(ServiceId target, uint8_t msgType);
    bool receive(ServiceId id, ServiceMessage& msg, TickType_t timeout);
    bool tryReceive(ServiceId id, ServiceMessage& msg);
    bool hasMessages(ServiceId id) const;
    QueueHandle_t getQueue(ServiceId id) const;
    bool query(ServiceId target, const ServiceMessage& request,
               ServiceMessage& reply, TickType_t timeout);

    // Heartbeat
    void heartbeat(ServiceId id);

    // ---- Arena allocators (bump, never freed — zero heap) ----
    void* arenaAlloc(size_t& used, uint8_t* arena, size_t arenaSize, size_t n) {
        size_t aligned = (n + 3) & ~3;
        if (used + aligned > arenaSize) return nullptr;
        void* ptr = arena + used;
        used += aligned;
        return ptr;
    }
    void* netAlloc(size_t n)    { return arenaAlloc(netArenaUsed,    netArena,    SR_NET_ARENA_SIZE,    n); }
    void* loggerAlloc(size_t n) { return arenaAlloc(loggerArenaUsed, loggerArena, SR_LOGGER_ARENA_SIZE, n); }
    void* fsAlloc(size_t n)     { return arenaAlloc(fsArenaUsed,     fsArena,     SR_FS_ARENA_SIZE,     n); }
    void* ioAlloc(size_t n)     { return arenaAlloc(ioArenaUsed,     ioArena,     SR_IO_ARENA_SIZE,     n); }
    void* stringAlloc(size_t n) { return arenaAlloc(stringArenaUsed, stringArena, SR_STRING_ARENA_SIZE, n); }
    void* fpAlloc(size_t n)     { return arenaAlloc(fpArenaUsed,     fpArena,     SR_FP_ARENA_SIZE,     n); }
    void* sysAlloc(size_t n)    { return arenaAlloc(sysArenaUsed,    sysArena,    SR_SYS_ARENA_SIZE,    n); }

    void arenaReset(size_t& used) { used = 0; }

    // Typed pool accessors
    LoggerMemory*           getLogger();
    StorageMemory*          getStorage();
    WiFiMemory*             getWiFi();
    PowerMemory*            getPower();
    SystemStatusMemory*     getSystemStatus();
    WeightServiceMemory*    getWeightService();
    MotionServiceMemory*    getMotionService();
    StateManagerMemory*     getStateManager();
    AccessControllerMemory* getAccessController();
    DoorServiceMemory*      getDoorService();
    ToolRepositoryMemory*   getToolRepository();
    UserRepositoryMemory*   getUserRepository();
    LogRepositoryMemory*    getLogRepository();
    WebServerMemory*        getWebServer();
    DisplayManagerMemory*   getDisplayManager();
    SerialCLIMemory*        getSerialCLI();
    ServerClientMemory*     getServerClient();

    // HAL pool access (raw bytes — caller uses placement new or direct pin access)
    void* getHALPool(ServiceId id);

    // Register a service mailbox with static queue storage
    bool registerMailbox(ServiceId id, UBaseType_t depth);

    // Boot count via RTC memory
    uint16_t readBootCount();
    void     incrementBootCount();
};

// ======================================================================
// COMPILE-TIME POOL SIZE VERIFICATION
// ======================================================================
static_assert(SR_POOL_KERNEL_SIZE >=
    sizeof(LoggerMemory) + sizeof(StorageMemory) + sizeof(WiFiMemory) +
    sizeof(PowerMemory) + sizeof(SystemStatusMemory),
    "Kernel pool too small");

static_assert(SR_POOL_HAL_SIZE >=
    HX711_POOL_SIZE + MPU6050_POOL_SIZE + SSD1306_POOL_SIZE +
    LCD1602_POOL_SIZE + FINGERPRINT_POOL_SIZE,
    "HAL pool too small");

static_assert(SR_POOL_DOMAIN_SIZE >=
    sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory) +
    sizeof(StateManagerMemory) + sizeof(AccessControllerMemory) +
    sizeof(DoorServiceMemory),
    "Domain pool too small");

static_assert(SR_POOL_DATA_SIZE >=
    sizeof(ToolRepositoryMemory) + sizeof(UserRepositoryMemory) +
    sizeof(LogRepositoryMemory),
    "Data pool too small");

static_assert(SR_POOL_PRESENTATION_SIZE >=
    sizeof(WebServerMemory) + sizeof(DisplayManagerMemory) +
    sizeof(SerialCLIMemory) + sizeof(ServerClientMemory),
    "Presentation pool too small");

// Arena sanity checks — must be ≥ minimum useful size
static_assert(SR_NET_ARENA_SIZE    >= 512, "Net arena undersized");
static_assert(SR_LOGGER_ARENA_SIZE >= 256, "Logger arena undersized");
static_assert(SR_IO_ARENA_SIZE     >= 256, "IO arena undersized");

// Total g_registry: ~73KB in .bss (fits in ESP32 DRAM with ~180KB free heap for WiFi stack)

// ======================================================================
// GLOBAL INSTANCE — single file-scope variable at link-time address
// ======================================================================
extern ServiceRegistry g_registry;

// ======================================================================
// OFFSET MACROS — compile-time constants for debugger
// ======================================================================
#define SR_OFFSET_HEADER              0
#define SR_OFFSET_SCB_ARRAY           16
#define SR_OFFSET_KERNEL_POOL         (SR_OFFSET_SCB_ARRAY + sizeof(ServiceControlBlock) * SR_MAX_SERVICES)
#define SR_OFFSET_HAL_POOL            (SR_OFFSET_KERNEL_POOL + SR_POOL_KERNEL_SIZE)
#define SR_OFFSET_DOMAIN_POOL         (SR_OFFSET_HAL_POOL + SR_POOL_HAL_SIZE)
#define SR_OFFSET_DATA_POOL           (SR_OFFSET_DOMAIN_POOL + SR_POOL_DOMAIN_SIZE)
#define SR_OFFSET_PRESENTATION_POOL   (SR_OFFSET_DATA_POOL + SR_POOL_DATA_SIZE)
#define SR_OFFSET_QUEUE_BUFFER_POOL   (SR_OFFSET_PRESENTATION_POOL + SR_POOL_PRESENTATION_SIZE)
#define SR_OFFSET_QUEUE_STRUCTS       (SR_OFFSET_QUEUE_BUFFER_POOL + SR_POOL_QUEUE_BUFFERS_SIZE)

// Domain pool offsets
#define SR_OFFSET_WEIGHT_SERVICE      (SR_OFFSET_DOMAIN_POOL + 0)
#define SR_OFFSET_MOTION_SERVICE      (SR_OFFSET_DOMAIN_POOL + sizeof(WeightServiceMemory))
#define SR_OFFSET_STATE_MANAGER       (SR_OFFSET_DOMAIN_POOL + sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory))
#define SR_OFFSET_ACCESS_CONTROLLER   (SR_OFFSET_DOMAIN_POOL + sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory) + sizeof(StateManagerMemory))
#define SR_OFFSET_DOOR_SERVICE        (SR_OFFSET_DOMAIN_POOL + sizeof(WeightServiceMemory) + sizeof(MotionServiceMemory) + sizeof(StateManagerMemory) + sizeof(AccessControllerMemory))

// Data pool offsets
#define SR_OFFSET_TOOL_REPOSITORY     (SR_OFFSET_DATA_POOL + 0)
#define SR_OFFSET_USER_REPOSITORY     (SR_OFFSET_DATA_POOL + sizeof(ToolRepositoryMemory))
#define SR_OFFSET_LOG_REPOSITORY      (SR_OFFSET_DATA_POOL + sizeof(ToolRepositoryMemory) + sizeof(UserRepositoryMemory))

// Presentation pool offsets
#define SR_OFFSET_WEB_SERVER          (SR_OFFSET_PRESENTATION_POOL + 0)
#define SR_OFFSET_DISPLAY_MANAGER     (SR_OFFSET_PRESENTATION_POOL + sizeof(WebServerMemory))
#define SR_OFFSET_SERIAL_CLI          (SR_OFFSET_PRESENTATION_POOL + sizeof(WebServerMemory) + sizeof(DisplayManagerMemory))
#define SR_OFFSET_SERVER_CLIENT       (SR_OFFSET_PRESENTATION_POOL + sizeof(WebServerMemory) + sizeof(DisplayManagerMemory) + sizeof(SerialCLIMemory))

// Kernel pool offsets
#define SR_OFFSET_LOGGER              (SR_OFFSET_KERNEL_POOL + 0)
#define SR_OFFSET_STORAGE             (SR_OFFSET_KERNEL_POOL + sizeof(LoggerMemory))
#define SR_OFFSET_WIFI_MEMORY         (SR_OFFSET_KERNEL_POOL + sizeof(LoggerMemory) + sizeof(StorageMemory))
#define SR_OFFSET_POWER_MEMORY        (SR_OFFSET_KERNEL_POOL + sizeof(LoggerMemory) + sizeof(StorageMemory) + sizeof(WiFiMemory))
#define SR_OFFSET_SYSTEM_STATUS       (SR_OFFSET_KERNEL_POOL + sizeof(LoggerMemory) + sizeof(StorageMemory) + sizeof(WiFiMemory) + sizeof(PowerMemory))

#endif // SERVICE_REGISTRY_H
