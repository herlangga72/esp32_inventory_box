#ifndef MATCHING_SERVICE_H
#define MATCHING_SERVICE_H

#include <Arduino.h>
#include <vector>
#include "../entities/Tool.h"

class ToolRepository;  // Forward declaration

class MatchingService {
public:
    MatchingService();
    
    Tool* matchByWeight(float delta, float tolerance = Config::DEFAULT_TOLERANCE);
    Tool* matchByWeightClosest(float delta, float tolerance = 10.0f);
    std::vector<Tool*> matchMultiple(float delta, int maxTools = Config::MAX_CONTENTS);
    
    float getMatchConfidence(Tool* tool, float delta);
    
    void setToolRepository(ToolRepository* repo);

private:
    ToolRepository* tools;
};

#endif // MATCHING_SERVICE_H