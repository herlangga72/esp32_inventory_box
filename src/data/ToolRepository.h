#ifndef TOOL_REPOSITORY_H
#define TOOL_REPOSITORY_H

#include <Arduino.h>
#include "../domain/entities/Tool.h"
#include "../kernel/ServiceRegistry.h"

class StorageManager;

// Free functions operating on ToolRepositoryMemory*
void tr_init(ToolRepositoryMemory* mem, StorageManager* storage);
int  tr_create(ToolRepositoryMemory* mem, StorageManager* storage, Tool* tool);
bool tr_update(ToolRepositoryMemory* mem, StorageManager* storage, int id, Tool* tool);
bool tr_remove(ToolRepositoryMemory* mem, StorageManager* storage, int id);
Tool* tr_findById(ToolRepositoryMemory* mem, StorageManager* storage, int id);

// Fills caller-provided buffer, returns count
int  tr_findAll(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools);
int  tr_findActive(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools);
int  tr_count(ToolRepositoryMemory* mem, StorageManager* storage);

// Serialization
void tr_serialize(const Tool& tool, char* buffer, size_t len);
Tool tr_deserialize(const char* buffer);

#endif
