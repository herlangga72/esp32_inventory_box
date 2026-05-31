#ifndef LOG_ENTRY_H
#define LOG_ENTRY_H

#include <Arduino.h>

struct LogEntry {
    time_t timestamp;
    int userId;
    int toolId;
    int severity;           // 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
    char event[20];         // tag (INIT, WIFI, STATE, etc.)
    char message[128];      // log message body
    float weightGrams;
    float deltaGrams;
    int motionType;

    LogEntry() : timestamp(0), userId(0), toolId(0), severity(3),
                 weightGrams(0), deltaGrams(0), motionType(0) {
        event[0] = '\0';
        message[0] = '\0';
    }

    void setEvent(const char* e) {
        strncpy(event, e, sizeof(event) - 1);
        event[sizeof(event) - 1] = '\0';
    }

    void setMessage(const char* m) {
        strncpy(message, m, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }
};

#endif // LOG_ENTRY_H