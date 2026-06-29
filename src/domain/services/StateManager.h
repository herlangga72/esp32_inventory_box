#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <Arduino.h>
#include "../../kernel/ServiceRegistry.h"

class StorageManager;  // forward declaration

// --- Initialization ---
void sm_init(StateManagerMemory* mem, StorageManager* storage);

// --- Mailbox entry point ---
void sm_dispatchMessage(StateManagerMemory* mem, const ServiceMessage& msg);

// --- Periodic time-based transitions ---
void sm_updatePeriodic(StateManagerMemory* mem);

// --- Read queries (used by WebServer) ---
int             sm_getCurrentState(const StateManagerMemory* mem);
int             sm_getCurrentUserId(const StateManagerMemory* mem);
float           sm_getBaseline(const StateManagerMemory* mem);

// --- For web API contents list ---
const int32_t*  sm_getContents(const StateManagerMemory* mem);
int             sm_getContentCount(const StateManagerMemory* mem);

// --- Manual contents management (for recalibration / API) ---
void sm_clearContents(StateManagerMemory* mem);

#endif // STATE_MANAGER_H
