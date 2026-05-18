#ifndef LOG_ENTRY_H
#define LOG_ENTRY_H

#include <Arduino.h>

struct LogEntry {
    time_t timestamp;
    int userId;
    int toolId;
    char event[20];
    float weightGrams;
    float deltaGrams;
    int motionType;
    
    LogEntry() : timestamp(0), userId(0), toolId(0),
                 weightGrams(0), deltaGrams(0), motionType(0) {
        event[0] = '\0';
    }
    
    void setEvent(const char* e) {
        strncpy(event, e, sizeof(event) - 1);
        event[sizeof(event) - 1] = '\0';
    }
};

#endif // LOG_ENTRY_H