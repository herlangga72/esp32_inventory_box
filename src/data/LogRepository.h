#ifndef LOG_REPOSITORY_H
#define LOG_REPOSITORY_H

#include <Arduino.h>
#include <vector>
#include "../domain/entities/LogEntry.h"

// Thin reader over SPIFFS log files (see utils/LogFile.h)
// Log writing is handled by LogManager queue + LogFile — this class
// provides read/filter/download access for web API and CLI.

class LogRepository {
public:
    LogRepository();

    // Read paginated, filtered log entries
    // Returns entries as LogEntry structs parsed from CSV
    std::vector<LogEntry> findFiltered(int limit = 50, int offset = 0,
                                       int minLevel = 0, const char* tag = nullptr);

    // Legacy-compatible: findAll(limit, offset) defaults to all levels
    std::vector<LogEntry> findAll(int limit = 50, int offset = 0);

    // Total log line count
    int count();

    // Download all log files as raw CSV string
    String downloadCSV();

    // Delete all log files
    void clear();

    // Get dropped message count from logger queue
    uint32_t getDropped();

    // Total file size in bytes
    size_t fileSize();

private:
    LogEntry parseCSVLine(const String& line);
};

#endif // LOG_REPOSITORY_H
