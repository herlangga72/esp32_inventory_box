#include "LogManager.h"
#include "LogFile.h"
#include "config/Config.h"

// ---- Module state ----
static QueueHandle_t logQueue = NULL;
static TaskHandle_t  logTaskHandle = NULL;
static volatile LogLevel currentLevel = LOG_INFO;
static volatile uint32_t droppedCount = 0;

// ---- Logger task ----
static void logTask(void* param) {
    LogMsg msg;

    while (true) {
        // Block until a message arrives
        if (xQueueReceive(logQueue, &msg, portMAX_DELAY) == pdTRUE) {
            // ---- Serial output ----
            const char* levelStr = "?";
            switch (msg.level) {
                case LOG_ERROR: levelStr = "ERROR"; break;
                case LOG_WARN:  levelStr = "WARN";  break;
                case LOG_INFO:  levelStr = "INFO";  break;
                case LOG_DEBUG: levelStr = "DEBUG"; break;
            }

            unsigned long ts = msg.timestamp;
            unsigned long hr  = ts / 3600000;
            unsigned long min = (ts / 60000) % 60;
            unsigned long sec = (ts / 1000) % 60;
            unsigned long ms  = ts % 1000;

            Serial.printf("[%02lu:%02lu:%02lu.%03lu] [%s] [%s] %s\n",
                hr, min, sec, ms, levelStr, msg.tag, msg.message);

            // ---- SPIFFS append ----
            logFileAppend(msg);
        }
    }
}

// ---- Public API ----

void logInit(UBaseType_t priority, uint16_t stackDepth) {
    if (logQueue != NULL) return;  // already initialized

    logQueue = xQueueCreate(Config::LOG_QUEUE_DEPTH, sizeof(LogMsg));
    if (!logQueue) {
        Serial.println("[LOG] FATAL: Failed to create log queue");
        return;
    }

    if (!logFileInit()) {
        Serial.println("[LOG] WARN: Log file init failed — file logging disabled");
    }

    if (xTaskCreate(logTask, "Logger", stackDepth, NULL, priority, &logTaskHandle) != pdPASS) {
        // Logger task failed — fall back to direct serial output for critical messages
        logQueue = NULL;  // disable queue-based logging
    } else {
        Serial.println("[LOG] Logger task started");
    }
}

void logSetLevel(LogLevel level) {
    currentLevel = level;
}

LogLevel logGetLevel() {
    return currentLevel;
}

uint32_t logGetDropped() {
    return droppedCount;
}

void logEnqueue(LogLevel level, const char* tag, const char* fmt, ...) {
    if (!logQueue) return;

    LogMsg msg;
    msg.timestamp = millis();
    msg.level = (uint8_t)level;

    // Copy tag (truncate to fit)
    strncpy(msg.tag, tag, LOG_TAG_MAX - 1);
    msg.tag[LOG_TAG_MAX - 1] = '\0';

    // Format message
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.message, LOG_MSG_MAX, fmt, args);
    va_end(args);

    // Non-blocking send — drop if queue full
    if (xQueueSend(logQueue, &msg, 0) != pdTRUE) {
        droppedCount++;
    }
}
