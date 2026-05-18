#ifndef USER_REPOSITORY_H
#define USER_REPOSITORY_H

#include <Arduino.h>
#include <vector>
#include "../domain/entities/User.h"
#include "StorageManager.h"
#include "config/Config.h"

class UserRepository {
public:
    UserRepository(StorageManager* storage);
    
    // CRUD
    int create(User* user);
    bool update(int id, User* user);
    bool remove(int id);
    User* findById(int id);
    
    // Query
    std::vector<User> findAll();
    std::vector<User> findActive();
    int count();
    
    // Authentication
    User* authenticate(const char* pin);
    
    // Stats
    void recordUsage(int userId, unsigned long durationSeconds);
    void recordPlacement(int userId);
    void recordRemoval(int userId);
    void resetStats(int userId);
    
    // Serialization
    void serialize(const User& user, char* buffer, size_t len);
    User deserialize(const char* buffer);

private:
    StorageManager* storage;
    User cache[Config::MAX_USERS];
    bool cacheValid;
    
    void invalidateCache();
    void loadCache();
};

#endif // USER_REPOSITORY_H