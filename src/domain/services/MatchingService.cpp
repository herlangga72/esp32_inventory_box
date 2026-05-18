#include "MatchingService.h"
#include "../../data/ToolRepository.h"
#include <algorithm>
#include <cmath>

MatchingService::MatchingService() : tools(nullptr) {}

void MatchingService::setToolRepository(ToolRepository* repo) {
    tools = repo;
}

Tool* MatchingService::matchByWeight(float delta, float tolerance) {
    if (!tools) return nullptr;
    
    auto allTools = tools->findActive();
    Tool* bestMatch = nullptr;
    float bestDiff = 999999.0f;
    
    for (auto& tool : allTools) {
        float diff = abs(delta - tool.weightGrams);
        if (diff <= tolerance && diff < bestDiff) {
            bestMatch = const_cast<Tool*>(&tool);
            bestDiff = diff;
        }
    }
    
    return bestMatch;
}

Tool* MatchingService::matchByWeightClosest(float delta, float tolerance) {
    if (!tools) return nullptr;
    
    auto allTools = tools->findActive();
    Tool* closest = nullptr;
    float minDiff = 999999.0f;
    
    for (auto& tool : allTools) {
        float diff = abs(delta - tool.weightGrams);
        if (diff < minDiff) {
            minDiff = diff;
            closest = const_cast<Tool*>(&tool);
        }
    }
    
    // Return only if within extended tolerance
    if (minDiff <= tolerance) {
        return closest;
    }
    return nullptr;
}

std::vector<Tool*> MatchingService::matchMultiple(float delta, int maxTools) {
    std::vector<Tool*> matches;
    if (!tools) return matches;
    
    float remainingDelta = delta;
    auto allTools = tools->findActive();
    
    // Sort by weight (largest first)
    std::sort(allTools.begin(), allTools.end(), 
        [](const Tool& a, const Tool& b) {
            return a.weightGrams > b.weightGrams;
        });
    
    for (auto& tool : allTools) {
        if (matches.size() >= (size_t)maxTools) break;
        
        if (remainingDelta >= tool.weightGrams - tool.toleranceGrams) {
            if (abs(remainingDelta - tool.weightGrams) <= tool.toleranceGrams) {
                matches.push_back(const_cast<Tool*>(&tool));
                remainingDelta -= tool.weightGrams;
            }
        }
    }
    
    return matches;
}

float MatchingService::getMatchConfidence(Tool* tool, float delta) {
    if (!tool) return 0.0f;
    
    float diff = abs(delta - tool->weightGrams);
    float confidence = 1.0f - (diff / tool->toleranceGrams);
    return std::max(0.0f, std::min(1.0f, confidence));
}