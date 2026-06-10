#include "MatchingService.h"

// ======================================================================
// ms_matchByWeight
// ======================================================================
// Iterates the tools array manually (no STL). For each active tool whose
// weightGrams lies within delta +/- toleranceGrams, writes the tool id to
// outIds. Returns the total number of matched tools (capped at maxResults).
// ======================================================================
int ms_matchByWeight(const Tool* tools, int toolCount, float delta, float tolerance,
                     int* outIds, int maxResults) {
    int matchCount = 0;

    for (int i = 0; i < toolCount && matchCount < maxResults; i++) {
        if (!tools[i].active) continue;

        float diff = abs(delta - tools[i].weightGrams);
        if (diff <= tools[i].toleranceGrams) {
            outIds[matchCount++] = tools[i].id;
        }
    }

    return matchCount;
}

// ======================================================================
// ms_matchClosest
// ======================================================================
// Finds the single active tool whose weightGrams is nearest to delta.
// Writes the tool id to *outId and a confidence score (0..1) to
// *outConfidence. Returns the matched tool id or -1 if no active tool found.
// ======================================================================
int ms_matchClosest(const Tool* tools, int toolCount, float delta,
                    int* outId, float* outConfidence) {
    int   bestId        = -1;
    float bestDiff      = 3.4028235e+38f;  // FLT_MAX without <cfloat>
    float bestTolerance = 0.0f;

    for (int i = 0; i < toolCount; i++) {
        if (!tools[i].active) continue;

        float diff = abs(delta - tools[i].weightGrams);
        if (diff < bestDiff) {
            bestDiff      = diff;
            bestId        = tools[i].id;
            bestTolerance = tools[i].toleranceGrams;
        }
    }

    if (outId && bestId >= 0) {
        *outId = bestId;
    }

    if (outConfidence) {
        if (bestId < 0 || bestTolerance <= 0.001f) {
            *outConfidence = 0.0f;
        } else {
            float conf = 1.0f - (bestDiff / bestTolerance);
            // Clamp to [0, 1]
            if (conf < 0.0f) conf = 0.0f;
            if (conf > 1.0f) conf = 1.0f;
            *outConfidence = conf;
        }
    }

    return bestId;
}
