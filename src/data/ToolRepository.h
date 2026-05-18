#ifndef TOOL_REPOSITORY_H
#define TOOL_REPOSITORY_H

#include <Arduino.h>
#include <vector>
#include "../domain/entities/Tool.h"
#include "StorageManager.h"
#include "config/Config.h"

class ToolRepository {
public:
    ToolRepository(StorageManager* storage);
    
    // CRUD
    int create(Tool* tool);
    bool update(int id, Tool* tool);
    bool remove(int id);
    Tool* findById(int id);
    
    // Query
    std::vector<Tool> findAll();
    std::vector<Tool> findActive();
    int count();
    
    // Serialization helpers
    void serialize(const Tool& tool, char* buffer, size_t len);
    Tool deserialize(const char* buffer);
    
private:
    StorageManager* storage;
    Tool cache[Config::MAX_TOOLS];
    bool cacheValid;
    
    void invalidateCache();
    void loadCache();
    void saveCache();
    char* getKey(int id, char* buffer);
};

#endif // TOOL_REPOSITORY_H