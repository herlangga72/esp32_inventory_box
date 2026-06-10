#include "LogRepository.h"
#include "../utils/LogFile.h"
#include "../utils/LogManager.h"
#include <cstring>
#include <cstdlib>

LogRepository::LogRepository() {}

struct CbData {
    LogEntry* buf;
    int bufSize;
    int minLevel;
    const char* tag;
    LogRepository* repo;
    int written;
    int limit;
};

static void filterCallback(const char* line, void* arg) {
    CbData* d = (CbData*)arg;
    if (d->written >= d->limit || d->written >= d->bufSize) return;

    LogEntry entry = d->repo->parseCSVLine(line);

    if (entry.severity < d->minLevel) return;
    if (d->tag && d->tag[0] && strcmp(entry.event, d->tag) != 0) return;

    d->buf[d->written++] = entry;
}

int LogRepository::findFiltered(LogEntry* buf, int bufSize, int limit, int offset,
                                 int minLevel, const char* tag) {
    CbData d;
    d.buf = buf;
    d.bufSize = bufSize;
    d.minLevel = minLevel;
    d.tag = tag;
    d.repo = this;
    d.written = 0;
    d.limit = limit;

    logFileRead(offset, limit, filterCallback, &d);
    return d.written;
}

int LogRepository::findAll(LogEntry* buf, int bufSize, int limit, int offset) {
    return findFiltered(buf, bufSize, limit, offset, 0, nullptr);
}

int LogRepository::count() {
    return logFileCount();
}

int LogRepository::downloadCSV(char* buf, int maxLen) {
    String csv = logFileGetAll();
    int len = csv.length();
    if (len >= maxLen) len = maxLen - 1;
    memcpy(buf, csv.c_str(), len);
    buf[len] = '\0';
    return len;
}

void LogRepository::clear() {
    logFileClear();
}

uint32_t LogRepository::getDropped() {
    return logGetDropped();
}

size_t LogRepository::fileSize() {
    return logFileSize();
}

// Parse CSV line without String heap — uses char* directly
LogEntry LogRepository::parseCSVLine(const char* line) {
    LogEntry entry;
    if (!line || !line[0]) return entry;

    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Parse: timestamp,level,tag,"message"
    char* p = buf;
    char* next = nullptr;

    // Timestamp
    char* comma = strchr(p, ',');
    if (comma) { *comma = '\0'; entry.timestamp = atol(p); p = comma + 1; }

    // Level
    comma = strchr(p, ',');
    if (comma) { *comma = '\0'; entry.severity = atoi(p); p = comma + 1; }

    // Tag (event field)
    comma = strchr(p, ',');
    if (comma) { *comma = '\0'; entry.setEvent(p); p = comma + 1; }

    // Message (quoted)
    if (*p == '"') {
        p++;
        char* endQuote = strrchr(p, '"');
        if (endQuote) *endQuote = '\0';
        entry.setMessage(p);
    }

    return entry;
}
