#include "ToolRepository.h"
#include <string.h>

ToolRepository::ToolRepository(StorageManager* storage) 
    : storage(storage), cacheValid(false) {}

int ToolRepository::create(Tool* tool) {
    int id = storage->getInt("tool_next_id", 1);
    tool->id = id;
    tool->createdAt = time(nullptr);
    tool->updatedAt = tool->createdAt;
    tool->active = true;
    
    // Serialize and save
    char key[16];
    char data[128];
    serialize(*tool, data, sizeof(data));
    
    snprintf(key, sizeof(key), "tool_%d", id);
    storage->putString(key, data);
    
    // Update metadata
    storage->putInt("tool_next_id", id + 1);
    storage->putInt("tool_count", count() + 1);
    
    invalidateCache();
    
    return id;
}

bool ToolRepository::update(int id, Tool* tool) {
    char key[16];
    char data[128];
    
    snprintf(key, sizeof(key), "tool_%d", id);
    
    tool->id = id;
    tool->updatedAt = time(nullptr);
    serialize(*tool, data, sizeof(data));
    
    storage->putString(key, data);
    invalidateCache();
    
    return true;
}

bool ToolRepository::remove(int id) {
    char key[16];
    snprintf(key, sizeof(key), "tool_%d", id);
    
    if (storage->remove(key)) {
        storage->putInt("tool_count", count() - 1);
        invalidateCache();
        return true;
    }
    return false;
}

Tool* ToolRepository::findById(int id) {
    if (!cacheValid) loadCache();
    
    for (int i = 0; i < count(); i++) {
        if (cache[i].id == id) {
            return &cache[i];
        }
    }
    return nullptr;
}

std::vector<Tool> ToolRepository::findAll() {
    if (!cacheValid) loadCache();
    
    std::vector<Tool> result;
    int n = storage->getInt("tool_count", 0);
    
    char key[16];
    char data[128];
    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "tool_%d", i);
        String dataStr = storage->getString(key, "");
        if (dataStr.length() > 0) {
            dataStr.toCharArray(data, sizeof(data));
            result.push_back(deserialize(data));
        }
    }
    
    return result;
}

std::vector<Tool> ToolRepository::findActive() {
    std::vector<Tool> all = findAll();
    std::vector<Tool> active;
    
    for (auto& tool : all) {
        if (tool.active) {
            active.push_back(tool);
        }
    }
    
    return active;
}

int ToolRepository::count() {
    return storage->getInt("tool_count", 0);
}

void ToolRepository::serialize(const Tool& tool, char* buffer, size_t len) {
    snprintf(buffer, len, "%d|%s|%.2f|%.2f|%d|%ld|%ld",
        tool.id,
        tool.name,
        tool.weightGrams,
        tool.toleranceGrams,
        tool.active ? 1 : 0,
        tool.createdAt,
        tool.updatedAt
    );
}

Tool ToolRepository::deserialize(const char* buffer) {
    Tool tool;
    
    char* p = strtok((char*)buffer, "|");
    if (p) tool.id = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) tool.setName(p);
    
    p = strtok(NULL, "|");
    if (p) tool.weightGrams = atof(p);
    
    p = strtok(NULL, "|");
    if (p) tool.toleranceGrams = atof(p);
    
    p = strtok(NULL, "|");
    if (p) tool.active = (atoi(p) == 1);
    
    p = strtok(NULL, "|");
    if (p) tool.createdAt = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) tool.updatedAt = atoi(p);
    
    return tool;
}

void ToolRepository::invalidateCache() {
    cacheValid = false;
}

void ToolRepository::loadCache() {
    int n = count();
    for (int i = 0; i < n && i < Config::MAX_TOOLS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "tool_%d", i);
        String dataStr = storage->getString(key, "");
        if (dataStr.length() > 0) {
            char data[128];
            dataStr.toCharArray(data, sizeof(data));
            cache[i] = deserialize(data);
        }
    }
    cacheValid = true;
}

void ToolRepository::saveCache() {
    int n = count();
    for (int i = 0; i < n && i < Config::MAX_TOOLS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "tool_%d", i);
        char data[128];
        serialize(cache[i], data, sizeof(data));
        storage->putString(key, data);
    }
}