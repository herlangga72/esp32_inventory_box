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
    char data[256];
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
    char data[256];
    
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
    char data[256];
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

// Tiny utility: extract value for JSON key like "n":"hammer" → "hammer"
static const char* repoJsonGet(const char* json, const char* key) {
    const char* pos = strstr(json, key);
    if (!pos) return nullptr;
    pos += strlen(key) + 2; // skip "key":
    if (*pos == '"') pos++;
    return pos;
}

void ToolRepository::serialize(const Tool& tool, char* buffer, size_t len) {
    snprintf(buffer, len, "%d|%s|%.1f|%.1f|%d|%ld|%ld",
        tool.id, tool.name, tool.weightGrams, tool.toleranceGrams,
        tool.active ? 1 : 0, tool.createdAt, tool.updatedAt);
}

Tool ToolRepository::deserialize(const char* buffer) {
    if (buffer[0] == '{') {
        // Migrate old JSON → new (re-saves as pipe on next write)
        Tool tool;
        const char* v;
        v = repoJsonGet(buffer, "\"n\""); if (v) { char s[32]; int i=0; while(*v && *v!='"' && i<31) s[i++]=*v++; s[i]=0; tool.setName(s); }
        v = repoJsonGet(buffer, "\"id\""); tool.id = v ? atoi(v) : 0;
        v = repoJsonGet(buffer, "\"w\""); tool.weightGrams = v ? atof(v) : 0.0f;
        v = repoJsonGet(buffer, "\"t\""); tool.toleranceGrams = v ? atof(v) : Config::DEFAULT_TOLERANCE;
        v = repoJsonGet(buffer, "\"a\""); tool.active = v ? (*v == 't') : true;
        v = repoJsonGet(buffer, "\"c\""); tool.createdAt = v ? atol(v) : 0;
        v = repoJsonGet(buffer, "\"u\""); tool.updatedAt = v ? atol(v) : 0;
        return tool;
    }
    return deserializeLegacy(buffer);
}

Tool ToolRepository::deserializeLegacy(const char* buffer) {
    Tool tool;
    char buf[256];
    strncpy(buf, buffer, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = strtok(buf, "|");
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
    if (p) tool.createdAt = atol(p);

    p = strtok(NULL, "|");
    if (p) tool.updatedAt = atol(p);

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
            char data[256];
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
        char data[256];
        serialize(cache[i], data, sizeof(data));
        storage->putString(key, data);
    }
}