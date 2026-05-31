#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Log levels (higher = more verbose)
enum LogLevel {
    LOG_NONE  = 0,
    LOG_ERROR = 1,
    LOG_WARN  = 2,
    LOG_INFO  = 3,
    LOG_DEBUG = 4
};

// Log message pushed to queue
#define LOG_TAG_MAX    8
#define LOG_MSG_MAX    128

struct LogMsg {
    unsigned long timestamp;  // millis() at enqueue time
    uint8_t      level;       // LogLevel
    char         tag[LOG_TAG_MAX];
    char         message[LOG_MSG_MAX];
};

// Public API
void logInit(UBaseType_t priority = 2, uint16_t stackDepth = 2560);
void logSetLevel(LogLevel level);
LogLevel logGetLevel();
uint32_t logGetDropped();

// Enqueue a formatted log message (called by macros)
void logEnqueue(LogLevel level, const char* tag, const char* fmt, ...);

// Macros — non-blocking, safe from any task or ISR
#define LOG_ERROR(tag, fmt, ...) do { \
    if (logGetLevel() >= LOG_ERROR) \
        logEnqueue(LOG_ERROR, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_WARN(tag, fmt, ...) do { \
    if (logGetLevel() >= LOG_WARN) \
        logEnqueue(LOG_WARN, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_INFO(tag, fmt, ...) do { \
    if (logGetLevel() >= LOG_INFO) \
        logEnqueue(LOG_INFO, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define LOG_DEBUG(tag, fmt, ...) do { \
    if (logGetLevel() >= LOG_DEBUG) \
        logEnqueue(LOG_DEBUG, tag, fmt, ##__VA_ARGS__); \
} while(0)

#endif // LOG_MANAGER_H
