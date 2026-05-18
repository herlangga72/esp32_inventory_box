#ifndef USER_ENTITY_H
#define USER_ENTITY_H

#include <Arduino.h>

struct User {
    int id;
    char name[32];
    char pin[5];  // 4-digit PIN + null
    bool active;
    time_t createdAt;
    
    // Stats
    unsigned long totalUsageSeconds;
    int sessionCount;
    int toolPlacements;
    int toolRemovals;
    
    User() : id(0), active(true), createdAt(0),
             totalUsageSeconds(0), sessionCount(0),
             toolPlacements(0), toolRemovals(0) {
        name[0] = '\0';
        pin[0] = '\0';
    }
    
    void setName(const char* n) {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    
    void setPin(const char* p) {
        strncpy(pin, p, sizeof(pin) - 1);
        pin[sizeof(pin) - 1] = '\0';
    }
    
    bool validatePin(const char* p) const {
        return strcmp(pin, p) == 0;
    }
};

#endif // USER_ENTITY_H