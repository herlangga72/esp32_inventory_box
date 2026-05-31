#include "LogRepository.h"
#include "../utils/LogFile.h"
#include "../utils/LogManager.h"
#include <string.h>

LogRepository::LogRepository() {}

std::vector<LogEntry> LogRepository::findFiltered(int limit, int offset,
                                                   int minLevel, const char* tag) {
    std::vector<LogEntry> results;

    // We need a callback to accumulate results
    struct CallbackData {
        std::vector<LogEntry>* results;
        int minLevel;
        const char* tag;
        LogRepository* repo;
        int returned;
        int limit;
    };

    CallbackData cbData;
    cbData.results = &results;
    cbData.minLevel = minLevel;
    cbData.tag = tag;
    cbData.repo = this;
    cbData.returned = 0;
    cbData.limit = limit;

    logFileRead(offset, limit,
        [](const char* line, void* arg) {
            CallbackData* d = (CallbackData*)arg;
            if (d->returned >= d->limit) return;

            LogEntry entry = d->repo->parseCSVLine(String(line));

            // Filter by level
            if (entry.severity < d->minLevel) return;

            // Filter by tag (tag is stored in entry.event)
            if (d->tag && d->tag[0] && strcmp(entry.event, d->tag) != 0) return;

            d->results->push_back(entry);
            d->returned++;
        },
        &cbData
    );

    return results;
}

std::vector<LogEntry> LogRepository::findAll(int limit, int offset) {
    return findFiltered(limit, offset, 0, nullptr);
}

int LogRepository::count() {
    return logFileCount();
}

String LogRepository::downloadCSV() {
    return logFileGetAll();
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

LogEntry LogRepository::parseCSVLine(const String& line) {
    LogEntry entry;

    // Parse: timestamp,level,tag,"message"
    int pos = 0;

    // Timestamp
    int comma1 = line.indexOf(',', pos);
    if (comma1 > 0) {
        entry.timestamp = line.substring(pos, comma1).toInt();
        pos = comma1 + 1;
    }

    // Level (severity)
    int comma2 = line.indexOf(',', pos);
    if (comma2 > 0) {
        entry.severity = line.substring(pos, comma2).toInt();
        pos = comma2 + 1;
    }

    // Tag -> event field
    int comma3 = line.indexOf(',', pos);
    if (comma3 > 0) {
        entry.setEvent(line.substring(pos, comma3).c_str());
        pos = comma3 + 1;
    }

    // Message (quoted) -> message field
    if (pos < (int)line.length() && line[pos] == '"') {
        pos++; // skip opening quote
        int endQuote = line.lastIndexOf('"');
        if (endQuote > pos) {
            entry.setMessage(line.substring(pos, endQuote).c_str());
        }
    }

    return entry;
}
