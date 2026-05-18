#include "UserRepository.h"
#include <string.h>

UserRepository::UserRepository(StorageManager* storage)
    : storage(storage), cacheValid(false) {}

int UserRepository::create(User* user) {
    int id = storage->getInt("user_next_id", 1);
    user->id = id;
    user->createdAt = time(nullptr);
    user->active = true;
    user->totalUsageSeconds = 0;
    user->sessionCount = 0;
    user->toolPlacements = 0;
    user->toolRemovals = 0;
    
    // Serialize and save
    char key[16];
    char data[256];
    serialize(*user, data, sizeof(data));
    
    snprintf(key, sizeof(key), "user_%d", id);
    storage->putString(key, data);
    
    // Update metadata
    storage->putInt("user_next_id", id + 1);
    storage->putInt("user_count", count() + 1);
    
    invalidateCache();
    
    return id;
}

bool UserRepository::update(int id, User* user) {
    char key[16];
    char data[256];
    
    snprintf(key, sizeof(key), "user_%d", id);
    
    user->id = id;
    serialize(*user, data, sizeof(data));
    
    storage->putString(key, data);
    invalidateCache();
    
    return true;
}

bool UserRepository::remove(int id) {
    char key[16];
    snprintf(key, sizeof(key), "user_%d", id);
    
    if (storage->remove(key)) {
        storage->putInt("user_count", count() - 1);
        invalidateCache();
        return true;
    }
    return false;
}

User* UserRepository::findById(int id) {
    if (!cacheValid) loadCache();
    
    for (int i = 0; i < count(); i++) {
        if (cache[i].id == id) {
            return &cache[i];
        }
    }
    return nullptr;
}

std::vector<User> UserRepository::findAll() {
    std::vector<User> result;
    int n = storage->getInt("user_count", 0);
    
    char key[16];
    char data[256];
    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "user_%d", i);
        String dataStr = storage->getString(key, "");
        if (dataStr.length() > 0) {
            dataStr.toCharArray(data, sizeof(data));
            result.push_back(deserialize(data));
        }
    }
    
    return result;
}

std::vector<User> UserRepository::findActive() {
    std::vector<User> all = findAll();
    std::vector<User> active;
    
    for (auto& user : all) {
        if (user.active) {
            active.push_back(user);
        }
    }
    
    return active;
}

int UserRepository::count() {
    return storage->getInt("user_count", 0);
}

User* UserRepository::authenticate(const char* pin) {
    auto all = findAll();
    
    for (auto& user : all) {
        if (user.active && user.validatePin(pin)) {
            return &user;
        }
    }
    return nullptr;
}

void UserRepository::recordUsage(int userId, unsigned long durationSeconds) {
    User* user = findById(userId);
    if (user) {
        user->totalUsageSeconds += durationSeconds;
        user->sessionCount++;
        update(userId, user);
    }
}

void UserRepository::recordPlacement(int userId) {
    User* user = findById(userId);
    if (user) {
        user->toolPlacements++;
        update(userId, user);
    }
}

void UserRepository::recordRemoval(int userId) {
    User* user = findById(userId);
    if (user) {
        user->toolRemovals++;
        update(userId, user);
    }
}

void UserRepository::resetStats(int userId) {
    User* user = findById(userId);
    if (user) {
        user->totalUsageSeconds = 0;
        user->sessionCount = 0;
        user->toolPlacements = 0;
        user->toolRemovals = 0;
        update(userId, user);
    }
}

void UserRepository::serialize(const User& user, char* buffer, size_t len) {
    snprintf(buffer, len, "%d|%s|%s|%d|%ld|%lu|%d|%d|%d",
        user.id,
        user.name,
        user.pin,
        user.active ? 1 : 0,
        user.createdAt,
        user.totalUsageSeconds,
        user.sessionCount,
        user.toolPlacements,
        user.toolRemovals
    );
}

User UserRepository::deserialize(const char* buffer) {
    User user;
    
    char* p = strtok((char*)buffer, "|");
    if (p) user.id = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) user.setName(p);
    
    p = strtok(NULL, "|");
    if (p) user.setPin(p);
    
    p = strtok(NULL, "|");
    if (p) user.active = (atoi(p) == 1);
    
    p = strtok(NULL, "|");
    if (p) user.createdAt = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) user.totalUsageSeconds = atol(p);
    
    p = strtok(NULL, "|");
    if (p) user.sessionCount = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) user.toolPlacements = atoi(p);
    
    p = strtok(NULL, "|");
    if (p) user.toolRemovals = atoi(p);
    
    return user;
}

void UserRepository::invalidateCache() {
    cacheValid = false;
}

void UserRepository::loadCache() {
    int n = count();
    for (int i = 0; i < n && i < Config::MAX_USERS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "user_%d", i);
        String dataStr = storage->getString(key, "");
        if (dataStr.length() > 0) {
            char data[256];
            dataStr.toCharArray(data, sizeof(data));
            cache[i] = deserialize(data);
        }
    }
    cacheValid = true;
}