#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <Arduino.h>
#include "LogManager.h"

// SPIFFS log file management
#define LOG_DIR         "/logs"
#define LOG_FILE_PATH   "/logs/events.csv"
#define LOG_FILE_MAX    102400   // 100 KB before rotation
#define LOG_ROTATE_KEEP 3        // Keep 3 rotated files

// Initialize log directory on SPIFFS
bool logFileInit();

// Append a CSV line: timestamp,level,tag,message\n
void logFileAppend(const LogMsg& msg);

// Read lines from current + rotated files, newest first
// Returns up to 'limit' lines starting at 'offset'
// Each line is a CSV row (caller frees each String)
int logFileRead(int offset, int limit,
                void (*callback)(const char* line, void* arg),
                void* arg);

// Concatenate all log files for download
String logFileGetAll();

// Total size of all log files in bytes
size_t logFileSize();

// Delete all log files
void logFileClear();

// Count total log lines across all files
int logFileCount();

#endif // LOG_FILE_H
