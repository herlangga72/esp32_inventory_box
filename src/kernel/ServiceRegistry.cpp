#include "ServiceRegistry.h"
#include "../utils/LogManager.h"

// RTC memory — survives deep sleep
RTC_DATA_ATTR uint16_t g_bootCount = 0;

// The single global registry instance
ServiceRegistry g_registry;

// ======================================================================
// POOL OFFSET TRACKING (run-time, set during init)
// ======================================================================
static size_t s_kernelOff = 0;
static size_t s_halOff = 0;
static size_t s_domainOff = 0;
static size_t s_dataOff = 0;
static size_t s_presentationOff = 0;
static size_t s_queueBufOff = 0;
static size_t s_queueStructIdx = 0;

// ======================================================================
// INIT
// ======================================================================
bool ServiceRegistry::init() {
    // Clear everything
    memset(this, 0, sizeof(ServiceRegistry));

    magic = SR_MAGIC;
    version = SR_VERSION;
    serviceCount = static_cast<uint8_t>(ServiceId::COUNT);
    bootCount = ++g_bootCount;
    uptimeMs = 0;

    // Compute checksum: XOR of first 7 bytes
    uint8_t* raw = reinterpret_cast<uint8_t*>(this);
    checksum = 0;
    for (int i = 0; i < 7; i++) checksum ^= raw[i];

    // Reset pool offsets
    s_kernelOff = 0;
    s_halOff = 0;
    s_domainOff = 0;
    s_dataOff = 0;
    s_presentationOff = 0;
    s_queueBufOff = 0;
    s_queueStructIdx = 0;

    // Initialize all SCBs to UNINIT
    for (int i = 0; i < SR_MAX_SERVICES; i++) {
        scb[i].id = static_cast<uint8_t>(i);
        scb[i].state = static_cast<uint8_t>(ServiceState::UNINIT);
        scb[i].inbox = nullptr;
        scb[i].task = nullptr;
    }

    LOG_INFO("REG", "INIT magic=0x%08X v=%d services=%d boot=%d",
             magic, version, serviceCount, bootCount);
    return true;
}

// ======================================================================
// SCB ACCESS
// ======================================================================
ServiceControlBlock* ServiceRegistry::getSCB(ServiceId id) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= SR_MAX_SERVICES) return nullptr;
    return &scb[idx];
}

// ======================================================================
// MAILBOX REGISTRATION
// ======================================================================
bool ServiceRegistry::registerMailbox(ServiceId id, UBaseType_t depth) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= SR_MAX_SERVICES) return false;
    if (scb[idx].inbox != nullptr) return true; // already registered

    size_t itemSize = sizeof(ServiceMessage);
    size_t bufSize = depth * itemSize;

    if (s_queueBufOff + bufSize > SR_POOL_QUEUE_BUFFERS_SIZE) {
        LOG_ERROR("REG", "Queue buffer pool exhausted for svc=%d", idx);
        return false;
    }
    if (s_queueStructIdx >= SR_MAX_SERVICES) {
        LOG_ERROR("REG", "Queue struct pool exhausted for svc=%d", idx);
        return false;
    }

    scb[idx].inbox = xQueueCreateStatic(
        depth, itemSize,
        &queueBufferPool[s_queueBufOff],
        &queueStructs[s_queueStructIdx]
    );

    if (!scb[idx].inbox) {
        LOG_ERROR("REG", "xQueueCreateStatic failed for svc=%d depth=%d", idx, depth);
        return false;
    }

    s_queueBufOff += bufSize;
    s_queueStructIdx++;
    scb[idx].flags |= SR_FLAG_HAS_INBOX;
    scb[idx].state = static_cast<uint8_t>(ServiceState::INIT);

    return true;
}

// ======================================================================
// MESSAGING
// ======================================================================
bool ServiceRegistry::send(ServiceId target, const ServiceMessage& msg) {
    uint8_t idx = static_cast<uint8_t>(target);
    if (idx >= SR_MAX_SERVICES || !scb[idx].inbox) return false;

    if (xQueueSend(scb[idx].inbox, &msg, 0) == pdTRUE) {
        scb[idx].msg_received++;
        return true;
    }
    scb[idx].msg_dropped++;
    return false;
}

bool ServiceRegistry::sendFromISR(ServiceId target, const ServiceMessage& msg,
                                   BaseType_t* pxHigherPriorityTaskWoken) {
    uint8_t idx = static_cast<uint8_t>(target);
    if (idx >= SR_MAX_SERVICES || !scb[idx].inbox) return false;

    if (xQueueSendFromISR(scb[idx].inbox, &msg, pxHigherPriorityTaskWoken) == pdTRUE) {
        scb[idx].msg_received++;
        return true;
    }
    scb[idx].msg_dropped++;
    return false;
}

bool ServiceRegistry::sendCmd(ServiceId target, uint8_t msgType) {
    ServiceMessage msg = ServiceMessage::cmd(target, msgType);
    return send(target, msg);
}

bool ServiceRegistry::receive(ServiceId id, ServiceMessage& msg, TickType_t timeout) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= SR_MAX_SERVICES || !scb[idx].inbox) return false;
    return (xQueueReceive(scb[idx].inbox, &msg, timeout) == pdTRUE);
}

bool ServiceRegistry::tryReceive(ServiceId id, ServiceMessage& msg) {
    return receive(id, msg, 0);
}

bool ServiceRegistry::hasMessages(ServiceId id) const {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= SR_MAX_SERVICES || !scb[idx].inbox) return false;
    return (uxQueueMessagesWaiting(scb[idx].inbox) > 0);
}

QueueHandle_t ServiceRegistry::getQueue(ServiceId id) const {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= SR_MAX_SERVICES) return nullptr;
    return scb[idx].inbox;
}

bool ServiceRegistry::query(ServiceId target, const ServiceMessage& request,
                              ServiceMessage& reply, TickType_t timeout) {
    // Send request
    if (!send(target, request)) return false;

    // Wait for reply on caller's own queue
    ServiceId replyTo = static_cast<ServiceId>(request.replyTo);
    return receive(replyTo, reply, timeout);
}

// ======================================================================
// HEARTBEAT
// ======================================================================
void ServiceRegistry::heartbeat(ServiceId id) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx < SR_MAX_SERVICES) {
        scb[idx].last_heartbeat_ms = millis();
        if (scb[idx].state == static_cast<uint8_t>(ServiceState::INIT)) {
            scb[idx].state = static_cast<uint8_t>(ServiceState::RUNNING);
        }
    }
}

// ======================================================================
// BOOT COUNT
// ======================================================================
uint16_t ServiceRegistry::readBootCount() {
    return g_bootCount;
}

void ServiceRegistry::incrementBootCount() {
    g_bootCount++;
}

// ======================================================================
// TYPED POOL ACCESSORS
// ======================================================================

LoggerMemory* ServiceRegistry::getLogger() {
    return reinterpret_cast<LoggerMemory*>(&kernelPool[SR_OFFSET_LOGGER - SR_OFFSET_KERNEL_POOL]);
}

StorageMemory* ServiceRegistry::getStorage() {
    return reinterpret_cast<StorageMemory*>(&kernelPool[SR_OFFSET_STORAGE - SR_OFFSET_KERNEL_POOL]);
}

WiFiMemory* ServiceRegistry::getWiFi() {
    return reinterpret_cast<WiFiMemory*>(&kernelPool[SR_OFFSET_WIFI_MEMORY - SR_OFFSET_KERNEL_POOL]);
}

PowerMemory* ServiceRegistry::getPower() {
    return reinterpret_cast<PowerMemory*>(&kernelPool[SR_OFFSET_POWER_MEMORY - SR_OFFSET_KERNEL_POOL]);
}

SystemStatusMemory* ServiceRegistry::getSystemStatus() {
    return reinterpret_cast<SystemStatusMemory*>(&kernelPool[SR_OFFSET_SYSTEM_STATUS - SR_OFFSET_KERNEL_POOL]);
}

WeightServiceMemory* ServiceRegistry::getWeightService() {
    return reinterpret_cast<WeightServiceMemory*>(&domainPool[SR_OFFSET_WEIGHT_SERVICE - SR_OFFSET_DOMAIN_POOL]);
}

MotionServiceMemory* ServiceRegistry::getMotionService() {
    return reinterpret_cast<MotionServiceMemory*>(&domainPool[SR_OFFSET_MOTION_SERVICE - SR_OFFSET_DOMAIN_POOL]);
}

StateManagerMemory* ServiceRegistry::getStateManager() {
    return reinterpret_cast<StateManagerMemory*>(&domainPool[SR_OFFSET_STATE_MANAGER - SR_OFFSET_DOMAIN_POOL]);
}

AccessControllerMemory* ServiceRegistry::getAccessController() {
    return reinterpret_cast<AccessControllerMemory*>(&domainPool[SR_OFFSET_ACCESS_CONTROLLER - SR_OFFSET_DOMAIN_POOL]);
}

DoorServiceMemory* ServiceRegistry::getDoorService() {
    return reinterpret_cast<DoorServiceMemory*>(&domainPool[SR_OFFSET_DOOR_SERVICE - SR_OFFSET_DOMAIN_POOL]);
}

ToolRepositoryMemory* ServiceRegistry::getToolRepository() {
    return reinterpret_cast<ToolRepositoryMemory*>(&dataPool[SR_OFFSET_TOOL_REPOSITORY - SR_OFFSET_DATA_POOL]);
}

UserRepositoryMemory* ServiceRegistry::getUserRepository() {
    return reinterpret_cast<UserRepositoryMemory*>(&dataPool[SR_OFFSET_USER_REPOSITORY - SR_OFFSET_DATA_POOL]);
}

LogRepositoryMemory* ServiceRegistry::getLogRepository() {
    return reinterpret_cast<LogRepositoryMemory*>(&dataPool[SR_OFFSET_LOG_REPOSITORY - SR_OFFSET_DATA_POOL]);
}

WebServerMemory* ServiceRegistry::getWebServer() {
    return reinterpret_cast<WebServerMemory*>(&presentationPool[SR_OFFSET_WEB_SERVER - SR_OFFSET_PRESENTATION_POOL]);
}

DisplayManagerMemory* ServiceRegistry::getDisplayManager() {
    return reinterpret_cast<DisplayManagerMemory*>(&presentationPool[SR_OFFSET_DISPLAY_MANAGER - SR_OFFSET_PRESENTATION_POOL]);
}

SerialCLIMemory* ServiceRegistry::getSerialCLI() {
    return reinterpret_cast<SerialCLIMemory*>(&presentationPool[SR_OFFSET_SERIAL_CLI - SR_OFFSET_PRESENTATION_POOL]);
}

ServerClientMemory* ServiceRegistry::getServerClient() {
    return reinterpret_cast<ServerClientMemory*>(&presentationPool[SR_OFFSET_SERVER_CLIENT - SR_OFFSET_PRESENTATION_POOL]);
}

void* ServiceRegistry::getHALPool(ServiceId id) {
    switch (id) {
        case ServiceId::HX711:       return &halPool[0];
        case ServiceId::MPU6050:     return &halPool[HX711_POOL_SIZE];
        case ServiceId::SSD1306:     return &halPool[HX711_POOL_SIZE + MPU6050_POOL_SIZE];
        case ServiceId::FINGERPRINT: return &halPool[HX711_POOL_SIZE + MPU6050_POOL_SIZE + SSD1306_POOL_SIZE];
        default: return nullptr;
    }
}
