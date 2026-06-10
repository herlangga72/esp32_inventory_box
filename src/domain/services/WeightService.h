#ifndef WEIGHT_SERVICE_H
#define WEIGHT_SERVICE_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"
#include "../../hal/HX711Driver.h"

// Free functions on WeightServiceMemory*
void ws_onRawReading(WeightServiceMemory* mem, int32_t raw);
void ws_update(WeightServiceMemory* mem);
float ws_getCurrentWeight(const WeightServiceMemory* mem);
float ws_getBaseline(const WeightServiceMemory* mem);
float ws_getDelta(const WeightServiceMemory* mem);

#endif
