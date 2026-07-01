#ifndef TOOL_ENTITY_H
#define TOOL_ENTITY_H

#include <Arduino.h>
#include "config/Config.h"

struct Tool {
    int id;
    char name[32];
    float weightGrams;
    float toleranceGrams;
    bool active;
    time_t createdAt;
    time_t updatedAt;
    
    Tool() : id(0), weightGrams(0), toleranceGrams(Config::DEFAULT_TOLERANCE),
             active(true), createdAt(0), updatedAt(0) {
        name[0] = '\0';
    }
    
    void setName(const char* n) {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    
    bool matchesWeight(float weight, float delta) const {
        float diff = fabsf(delta - weightGrams);
        return diff <= toleranceGrams;
    }
};

#endif // TOOL_ENTITY_H