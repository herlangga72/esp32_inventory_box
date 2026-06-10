#ifndef LOG_REPOSITORY_H
#define LOG_REPOSITORY_H

#include <Arduino.h>
#include "../domain/entities/LogEntry.h"

// Thin reader over SPIFFS log files (see utils/LogFile.h)
// No STL, no heap — caller provides buffers.

class LogRepository {
public:
    LogRepository();

    // Fill caller-provided buffer. Returns count written, max = bufSize.
    int findFiltered(LogEntry* buf, int bufSize, int limit = 50, int offset = 0,
                     int minLevel = 0, const char* tag = nullptr);
    int findAll(LogEntry* buf, int bufSize, int limit = 50, int offset = 0);

    // Total log line count
    int count();

    // Download CSV into caller buffer. Returns bytes written (excluding null).
    int downloadCSV(char* buf, int maxLen);

    // Delete all log files
    void clear();

    // Get dropped message count from logger queue
    uint32_t getDropped();

    // Total file size in bytes
    size_t fileSize();

    LogEntry parseCSVLine(const char* line);  // public for callback access
};

#endif // LOG_REPOSITORY_H
