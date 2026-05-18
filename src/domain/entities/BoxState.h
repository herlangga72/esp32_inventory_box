#ifndef BOX_STATE_H
#define BOX_STATE_H

#include <Arduino.h>
#include "config/Config.h"

enum class BoxStateMachine {
    INIT,
    IDLE,
    ANALYZING,
    TOOL_PLACED,
    REMOVING,
    UNKNOWN_ITEM,
    CALIBRATING,
    ERROR,
    SLEEP
};

struct BoxState {
    float baselineGrams;
    float currentWeightGrams;
    float previousWeightGrams;
    int contents[Config::MAX_CONTENTS];
    int contentCount;
    int currentUserId;
    time_t sessionStart;
    BoxStateMachine state;
    time_t lastEventTime;
    
    BoxState() : baselineGrams(0), currentWeightGrams(0), previousWeightGrams(0),
                 contentCount(0), currentUserId(0), sessionStart(0),
                 state(BoxStateMachine::INIT), lastEventTime(0) {
        memset(contents, 0, sizeof(contents));
    }
    
    void reset() {
        contentCount = 0;
        memset(contents, 0, sizeof(contents));
        currentWeightGrams = 0;
        previousWeightGrams = 0;
    }
    
    bool addTool(int toolId) {
        if (contentCount >= Config::MAX_CONTENTS) return false;
        for (int i = 0; i < contentCount; i++) {
            if (contents[i] == toolId) return false;  // Already in box
        }
        contents[contentCount++] = toolId;
        return true;
    }
    
    bool removeTool(int toolId) {
        for (int i = 0; i < contentCount; i++) {
            if (contents[i] == toolId) {
                // Shift remaining tools
                for (int j = i; j < contentCount - 1; j++) {
                    contents[j] = contents[j + 1];
                }
                contentCount--;
                return true;
            }
        }
        return false;
    }
    
    bool hasTool(int toolId) const {
        for (int i = 0; i < contentCount; i++) {
            if (contents[i] == toolId) return true;
        }
        return false;
    }
};

#endif // BOX_STATE_H