#include "UserRepository.h"
#include "StorageManager.h"
#include "../config/Config.h"
#include <cstring>
#include <cstdlib>

void ur_init(UserRepositoryMemory* mem, StorageManager* storage) {
    memset(mem, 0, sizeof(UserRepositoryMemory));
    mem->cacheValid = false;
}

int ur_create(UserRepositoryMemory* mem, StorageManager* storage, User* user) {
    int id = storage->getInt("user_next_id", 0);
    user->id = id;
    user->createdAt = time(nullptr);
    user->active = true;
    user->totalUsageSeconds = 0;
    user->sessionCount = 0;
    user->toolPlacements = 0;
    user->toolRemovals = 0;

    char key[16], data[256];
    ur_serialize(*user, data, sizeof(data));
    snprintf(key, sizeof(key), "user_%d", id);
    storage->putString(key, data);

    storage->putInt("user_next_id", id + 1);
    storage->putInt("user_count", ur_count(mem, storage) + 1);
    mem->cacheValid = false;
    return id;
}

bool ur_update(UserRepositoryMemory* mem, StorageManager* storage, int id, User* user) {
    char key[16], data[256];
    snprintf(key, sizeof(key), "user_%d", id);
    user->id = id;
    ur_serialize(*user, data, sizeof(data));
    storage->putString(key, data);
    mem->cacheValid = false;
    return true;
}

bool ur_remove(UserRepositoryMemory* mem, StorageManager* storage, int id) {
    char key[16];
    snprintf(key, sizeof(key), "user_%d", id);
    if (storage->remove(key)) {
        storage->putInt("user_count", ur_count(mem, storage) - 1);
        mem->cacheValid = false;
        return true;
    }
    return false;
}

static void loadCache(UserRepositoryMemory* mem, StorageManager* storage) {
    int n = ur_count(mem, storage);
    for (int i = 0; i < n && i < Config::MAX_USERS; i++) {
        char key[16], data[256];
        snprintf(key, sizeof(key), "user_%d", i);
        if (storage->getChars(key, data, sizeof(data)) > 0) {
            mem->cache[i] = ur_deserialize(data);
        }
    }
    mem->cacheValid = true;
}

User* ur_findById(UserRepositoryMemory* mem, StorageManager* storage, int id) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = ur_count(mem, storage);
    for (int i = 0; i < n && i < Config::MAX_USERS; i++) {
        if (mem->cache[i].id == id) return &mem->cache[i];
    }
    return nullptr;
}

int ur_findAll(UserRepositoryMemory* mem, StorageManager* storage, User* outBuf, int maxUsers) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = ur_count(mem, storage);
    int count = 0;
    for (int i = 0; i < n && count < maxUsers; i++) {
        outBuf[count++] = mem->cache[i];
    }
    return count;
}

int ur_count(UserRepositoryMemory* mem, StorageManager* storage) {
    return storage->getInt("user_count", 0);
}

User* ur_authenticate(UserRepositoryMemory* mem, StorageManager* storage, const char* pin) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = ur_count(mem, storage);
    for (int i = 0; i < n && i < Config::MAX_USERS; i++) {
        if (mem->cache[i].active && strcmp(mem->cache[i].pin, pin) == 0) {
            return &mem->cache[i];
        }
    }
    return nullptr;
}

User* ur_findByFingerprintId(UserRepositoryMemory* mem, StorageManager* storage, int fpId) {
    if (!mem->cacheValid) loadCache(mem, storage);
    int n = ur_count(mem, storage);
    for (int i = 0; i < n && i < Config::MAX_USERS; i++) {
        if (mem->cache[i].active && mem->cache[i].fpId == fpId) {
            return &mem->cache[i];
        }
    }
    return nullptr;
}

void ur_serialize(const User& user, char* buffer, size_t len) {
    snprintf(buffer, len, "%d|%s|%s|%d|%ld|%d|%lu|%d|%d|%d",
        user.id, user.name, user.pin,
        user.active ? 1 : 0, (long)user.createdAt, user.fpId,
        (unsigned long)user.totalUsageSeconds, user.sessionCount,
        user.toolPlacements, user.toolRemovals);
}

User ur_deserialize(const char* buffer) {
    User user;
    if (buffer[0] == '{') {
        // JSON fallback
        const char* v;
        auto getJson = [](const char* json, const char* key) -> const char* {
            const char* pos = strstr(json, key);
            if (!pos) return nullptr;
            pos += strlen(key) + 2;
            if (*pos == '"') pos++;
            return pos;
        };
        v = getJson(buffer, "\"n\""); if (v) { char s[32]; int i=0; while(*v && *v!='"' && i<31) s[i++]=*v++; s[i]=0; user.setName(s); }
        v = getJson(buffer, "\"p\""); if (v) { char s[8]; int i=0; while(*v && *v!='"' && i<7) s[i++]=*v++; s[i]=0; user.setPin(s); }
        v = getJson(buffer, "\"id\""); user.id = v ? atoi(v) : 0;
        v = getJson(buffer, "\"a\""); user.active = v ? (*v == 't' || *v == '1') : true;
        v = getJson(buffer, "\"f\""); user.fpId = v ? atoi(v) : 0;
        v = getJson(buffer, "\"c\""); user.createdAt = v ? atol(v) : 0;
        return user;
    }

    // Pipe-delimited
    char buf[256];
    strncpy(buf, buffer, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* p = strtok(buf, "|");
    if (p) user.id = atoi(p);
    p = strtok(NULL, "|"); if (p) user.setName(p);
    p = strtok(NULL, "|"); if (p) user.setPin(p);
    p = strtok(NULL, "|"); if (p) user.active = (atoi(p) == 1);
    p = strtok(NULL, "|"); if (p) user.createdAt = atol(p);
    p = strtok(NULL, "|"); if (p) user.fpId = atoi(p);
    p = strtok(NULL, "|"); if (p) user.totalUsageSeconds = atol(p);
    p = strtok(NULL, "|"); if (p) user.sessionCount = atoi(p);
    p = strtok(NULL, "|"); if (p) user.toolPlacements = atoi(p);
    p = strtok(NULL, "|"); if (p) user.toolRemovals = atoi(p);
    return user;
}
