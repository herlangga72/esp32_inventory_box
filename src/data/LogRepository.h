#ifndef LOG_REPOSITORY_H
#define LOG_REPOSITORY_H

#include <Arduino.h>
#include <vector>
#include "../domain/entities/LogEntry.h"
#include "StorageManager.h"
#include "config/Config.h"

class LogRepository {
public:
    LogRepository(StorageManager* storage);
    
    void log(const LogEntry& entry);
    std::vector<LogEntry> findAll(int limit = 50, int offset = 0);
    std::vector<LogEntry> findByUser(int userId);
    std::vector<LogEntry> findByTool(int toolId);
    int count();
    void clear();
    
    String exportJSON();

private:
    StorageManager* storage;
    int writeIndex;
    int logCount;
    
    void incrementWriteIndex();
    char* getKey(int idx, char* buffer);
};

#endif // LOG_REPOSITORY_H