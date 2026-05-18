#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

class StorageManager {
public:
    bool begin(const char* ns = "inventory");
    
    // Integer
    int getInt(const char* key, int defaultValue = 0);
    void putInt(const char* key, int value);
    
    // Float
    float getFloat(const char* key, float defaultValue = 0.0f);
    void putFloat(const char* key, float value);
    
    // String
    String getString(const char* key, const char* defaultValue = "");
    void putString(const char* key, const char* value);
    
    // Bool
    bool getBool(const char* key, bool defaultValue = false);
    void putBool(const char* key, bool value);
    
    // Blob
    size_t getBlob(const char* key, void* buffer, size_t len);
    void putBlob(const char* key, const void* buffer, size_t len);
    
    // Management
    bool remove(const char* key);
    void clear();

private:
    Preferences prefs;
    const char* namespace;
};

#endif // STORAGE_MANAGER_H