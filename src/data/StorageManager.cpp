#include "StorageManager.h"

bool StorageManager::begin(const char* ns) {
    nvsNamespace = ns;
    return prefs.begin(ns, false);  // read-only = false (allows writes)
}

int StorageManager::getInt(const char* key, int defaultValue) {
    return prefs.getInt(key, defaultValue);
}

void StorageManager::putInt(const char* key, int value) {
    prefs.putInt(key, value);
}

float StorageManager::getFloat(const char* key, float defaultValue) {
    return prefs.getFloat(key, defaultValue);
}

void StorageManager::putFloat(const char* key, float value) {
    prefs.putFloat(key, value);
}

String StorageManager::getString(const char* key, const char* defaultValue) {
    return prefs.getString(key, defaultValue);
}

void StorageManager::putString(const char* key, const char* value) {
    prefs.putString(key, value);
}

bool StorageManager::getBool(const char* key, bool defaultValue) {
    return prefs.getBool(key, defaultValue);
}

void StorageManager::putBool(const char* key, bool value) {
    prefs.putBool(key, value);
}

size_t StorageManager::getBlob(const char* key, void* buffer, size_t len) {
    return prefs.getBytes(key, buffer, len);
}

void StorageManager::putBlob(const char* key, const void* buffer, size_t len) {
    prefs.putBytes(key, (const uint8_t*)buffer, len);
}

bool StorageManager::remove(const char* key) {
    return prefs.remove(key);
}

void StorageManager::clear() {
    prefs.clear();
}