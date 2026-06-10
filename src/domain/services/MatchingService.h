#ifndef MATCHING_SERVICE_H
#define MATCHING_SERVICE_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"

// Match tools by weight delta. Writes matched tool IDs to outIds (max 10).
// Returns number of matches.
int ms_matchByWeight(const Tool* tools, int toolCount, float delta, float tolerance,
                     int* outIds, int maxResults);

// Find single closest match. Returns toolId (or -1 if no match),
// writes confidence (0..1) to *outConfidence.
int ms_matchClosest(const Tool* tools, int toolCount, float delta,
                    int* outId, float* outConfidence);

#endif // MATCHING_SERVICE_H
