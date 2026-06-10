#ifndef USER_REPOSITORY_H
#define USER_REPOSITORY_H

#include <Arduino.h>
#include "../domain/entities/User.h"
#include "../kernel/ServiceRegistry.h"

class StorageManager;

// Free functions on UserRepositoryMemory*
void ur_init(UserRepositoryMemory* mem, StorageManager* storage);
int  ur_create(UserRepositoryMemory* mem, StorageManager* storage, User* user);
bool ur_update(UserRepositoryMemory* mem, StorageManager* storage, int id, User* user);
bool ur_remove(UserRepositoryMemory* mem, StorageManager* storage, int id);
User* ur_findById(UserRepositoryMemory* mem, StorageManager* storage, int id);
int  ur_findAll(UserRepositoryMemory* mem, StorageManager* storage, User* outBuf, int maxUsers);
int  ur_count(UserRepositoryMemory* mem, StorageManager* storage);
User* ur_authenticate(UserRepositoryMemory* mem, StorageManager* storage, const char* pin);
User* ur_findByFingerprintId(UserRepositoryMemory* mem, StorageManager* storage, int fpId);

void ur_serialize(const User& user, char* buffer, size_t len);
User ur_deserialize(const char* buffer);

#endif
