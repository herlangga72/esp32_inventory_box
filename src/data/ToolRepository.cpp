#include "ToolRepository.h"
#include "StorageManager.h"
#include "../config/Config.h"
#include <cstring>
#include <cstdlib>

void tr_init(ToolRepositoryMemory* mem, StorageManager* storage) {
    memset(mem, 0, sizeof(ToolRepositoryMemory));
    mem->cacheValid = false;
}

int tr_create(ToolRepositoryMemory* mem, StorageManager* storage, Tool* tool) {
    int id = storage->getInt("tool_next_id", 1);
    tool->id = id;
    tool->createdAt = time(nullptr);
    tool->updatedAt = tool->createdAt;
    tool->active = true;

    char key[16], data[256];
    tr_serialize(*tool, data, sizeof(data));
    snprintf(key, sizeof(key), "tool_%d", id);
    storage->putString(key, data);

    storage->putInt("tool_next_id", id + 1);
    storage->putInt("tool_count", tr_count(mem, storage) + 1);

    mem->cacheValid = false;
    return id;
}

bool tr_update(ToolRepositoryMemory* mem, StorageManager* storage, int id, Tool* tool) {
    char key[16], data[256];
    snprintf(key, sizeof(key), "tool_%d", id);
    tool->id = id;
    tool->updatedAt = time(nullptr);
    tr_serialize(*tool, data, sizeof(data));
    storage->putString(key, data);
    mem->cacheValid = false;
    return true;
}

bool tr_remove(ToolRepositoryMemory* mem, StorageManager* storage, int id) {
    char key[16];
    snprintf(key, sizeof(key), "tool_%d", id);
    if (storage->remove(key)) {
        storage->putInt("tool_count", tr_count(mem, storage) - 1);
        mem->cacheValid = false;
        return true;
    }
    return false;
}

static void loadCache(ToolRepositoryMemory* mem, StorageManager* storage) {
    int n = tr_count(mem, storage);
    for (int i = 0; i < n && i < Config::MAX_TOOLS; i++) {
        char key[16], data[256];
        snprintf(key, sizeof(key), "tool_%d", i);
        if (storage->getChars(key, data, sizeof(data)) > 0) {
            mem->cache[i] = tr_deserialize(data);
        }
    }
    mem->cacheValid = true;
}

Tool* tr_findById(ToolRepositoryMemory* mem, StorageManager* storage, int id) {
    if (!mem->cacheValid) loadCache(mem, storage);
    for (int i = 0; i < tr_count(mem, storage); i++) {
        if (mem->cache[i].id == id) return &mem->cache[i];
    }
    return nullptr;
}

int tr_findAll(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = tr_count(mem, storage);
    int count = 0;
    for (int i = 0; i < n && count < maxTools; i++) {
        outBuf[count++] = mem->cache[i];
    }
    return count;
}

int tr_findActive(ToolRepositoryMemory* mem, StorageManager* storage, Tool* outBuf, int maxTools) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = tr_count(mem, storage);
    int count = 0;
    for (int i = 0; i < n && count < maxTools; i++) {
        if (mem->cache[i].active) {
            outBuf[count++] = mem->cache[i];
        }
    }
    return count;
}

int tr_count(ToolRepositoryMemory* mem, StorageManager* storage) {
    return storage->getInt("tool_count", 0);
}

void tr_serialize(const Tool& tool, char* buffer, size_t len) {
    snprintf(buffer, len, "%d|%s|%.1f|%.1f|%d|%ld|%ld",
        tool.id, tool.name, tool.weightGrams, tool.toleranceGrams,
        tool.active ? 1 : 0, (long)tool.createdAt, (long)tool.updatedAt);
}

static const char* repoJsonGet(const char* json, const char* key) {
    const char* pos = strstr(json, key);
    if (!pos) return nullptr;
    pos += strlen(key) + 2;
    if (*pos == '"') pos++;
    return pos;
}

Tool tr_deserialize(const char* buffer) {
    Tool tool;
    if (buffer[0] == '{') {
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

    // Pipe-delimited deserialization
    char buf[256];
    strncpy(buf, buffer, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* p = strtok(buf, "|");
    if (p) tool.id = atoi(p);
    p = strtok(NULL, "|"); if (p) tool.setName(p);
    p = strtok(NULL, "|"); if (p) tool.weightGrams = atof(p);
    p = strtok(NULL, "|"); if (p) tool.toleranceGrams = atof(p);
    p = strtok(NULL, "|"); if (p) tool.active = (atoi(p) == 1);
    p = strtok(NULL, "|"); if (p) tool.createdAt = atol(p);
    p = strtok(NULL, "|"); if (p) tool.updatedAt = atol(p);
    return tool;
}
