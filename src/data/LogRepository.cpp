#include "LogRepository.h"
#include <string.h>

LogRepository::LogRepository(StorageManager* storage)
    : storage(storage), writeIndex(0), logCount(0) {
    writeIndex = storage->getInt("log_write_idx", 0);
    logCount = storage->getInt("log_count", 0);
}

void LogRepository::log(const LogEntry& entry) {
    char key[16];
    char data[128];
    
    snprintf(data, sizeof(data), "%ld|%d|%d|%s|%.2f|%.2f|%d",
        entry.timestamp,
        entry.userId,
        entry.toolId,
        entry.event,
        entry.weightGrams,
        entry.deltaGrams,
        entry.motionType
    );
    
    snprintf(key, sizeof(key), "log_%d", writeIndex);
    storage->putString(key, data);
    
    incrementWriteIndex();
}

std::vector<LogEntry> LogRepository::findAll(int limit, int offset) {
    std::vector<LogEntry> result;
    
    int startIdx = (writeIndex - 1 - offset + Config::MAX_LOGS) % Config::MAX_LOGS;
    int count = 0;
    
    for (int i = 0; i < limit && count < logCount; i++) {
        int idx = (startIdx - i + Config::MAX_LOGS) % Config::MAX_LOGS;
        
        char key[16];
        snprintf(key, sizeof(key), "log_%d", idx);
        
        String dataStr = storage->getString(key, "");
        if (dataStr.length() > 0) {
            char data[128];
            dataStr.toCharArray(data, sizeof(data));
            
            LogEntry entry;
            char* p = strtok(data, "|");
            if (p) entry.timestamp = atol(p);
            
            p = strtok(NULL, "|");
            if (p) entry.userId = atoi(p);
            
            p = strtok(NULL, "|");
            if (p) entry.toolId = atoi(p);
            
            p = strtok(NULL, "|");
            if (p) entry.setEvent(p);
            
            p = strtok(NULL, "|");
            if (p) entry.weightGrams = atof(p);
            
            p = strtok(NULL, "|");
            if (p) entry.deltaGrams = atof(p);
            
            p = strtok(NULL, "|");
            if (p) entry.motionType = atoi(p);
            
            result.push_back(entry);
            count++;
        }
    }
    
    return result;
}

std::vector<LogEntry> LogRepository::findByUser(int userId) {
    auto all = findAll(Config::MAX_LOGS, 0);
    std::vector<LogEntry> filtered;
    
    for (auto& entry : all) {
        if (entry.userId == userId) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

std::vector<LogEntry> LogRepository::findByTool(int toolId) {
    auto all = findAll(Config::MAX_LOGS, 0);
    std::vector<LogEntry> filtered;
    
    for (auto& entry : all) {
        if (entry.toolId == toolId) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

int LogRepository::count() {
    return logCount;
}

void LogRepository::clear() {
    char key[16];
    for (int i = 0; i < Config::MAX_LOGS; i++) {
        snprintf(key, sizeof(key), "log_%d", i);
        storage->remove(key);
    }
    
    writeIndex = 0;
    logCount = 0;
    storage->putInt("log_write_idx", 0);
    storage->putInt("log_count", 0);
}

String LogRepository::exportJSON() {
    String json = "{\"logs\":[";
    
    auto logs = findAll(100, 0);
    bool first = true;
    
    for (auto& entry : logs) {
        if (!first) json += ",";
        first = false;
        
        json += "{";
        json += "\"timestamp\":" + String(entry.timestamp) + ",";
        json += "\"userId\":" + String(entry.userId) + ",";
        json += "\"toolId\":" + String(entry.toolId) + ",";
        json += "\"event\":\"" + String(entry.event) + "\",";
        json += "\"weightGrams\":" + String(entry.weightGrams, 2) + ",";
        json += "\"deltaGrams\":" + String(entry.deltaGrams, 2);
        json += "}";
    }
    
    json += "]}";
    return json;
}

void LogRepository::incrementWriteIndex() {
    writeIndex = (writeIndex + 1) % Config::MAX_LOGS;
    if (logCount < Config::MAX_LOGS) {
        logCount++;
    }
    
    storage->putInt("log_write_idx", writeIndex);
    storage->putInt("log_count", logCount);
}