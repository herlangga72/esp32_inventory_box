#include "LogFile.h"
#include <SPIFFS.h>
#include "../hal/InterruptManager.h"

#define SPIFFS_READ_CHUNK 256
#define SPIFFS_DOWNLOAD_MAX 32768  // cap download at 32KB to prevent OOM

// ---- Internal helpers ----

static String makeRotatedPath(int n) {
    return String(LOG_DIR) + "/events." + String(n) + ".csv";
}

static void rotateFiles() {
    // Delete oldest rotated file
    String oldest = makeRotatedPath(LOG_ROTATE_KEEP - 1);
    if (SPIFFS.exists(oldest)) {
        SPIFFS.remove(oldest);
    }

    // Shift: events.1.csv → events.2.csv, events.0.csv → events.1.csv
    if (SPIFFS.exists(makeRotatedPath(1))) {
        SPIFFS.rename(makeRotatedPath(1), makeRotatedPath(2));
    }
    if (SPIFFS.exists(makeRotatedPath(0))) {
        SPIFFS.rename(makeRotatedPath(0), makeRotatedPath(1));
    }
    // events.csv → events.0.csv
    if (SPIFFS.exists(LOG_FILE_PATH)) {
        SPIFFS.rename(LOG_FILE_PATH, makeRotatedPath(0));
    }
}

// ---- Public API ----

bool logFileInit() {
    spiffsLock();
    if (!SPIFFS.begin(true)) {
        spiffsUnlock();
        return false;
    }

    File test = SPIFFS.open(LOG_FILE_PATH, "a");
    if (!test) {
        spiffsUnlock();
        return false;
    }
    test.close();
    spiffsUnlock();
    return true;
}

void logFileAppend(const LogMsg& msg) {
    spiffsLock();
    File f = SPIFFS.open(LOG_FILE_PATH, "a");
    if (!f) { spiffsUnlock(); return; }

    f.print(msg.timestamp);
    f.print(',');
    f.print(msg.level);
    f.print(',');
    f.print(msg.tag);
    f.print(',');
    f.print('"');
    for (int i = 0; i < LOG_MSG_MAX && msg.message[i]; i++) {
        if (msg.message[i] == '"') f.print('"');
        f.print(msg.message[i]);
    }
    f.print('"');
    f.print('\n');

    f.close();

    if (f.size() > LOG_FILE_MAX) {
        rotateFiles();
    }
    spiffsUnlock();
}

int logFileRead(int offset, int limit,
                void (*callback)(const char* line, void* arg),
                void* arg) {
    // Collect all files: events.csv, events.0.csv, events.1.csv, events.2.csv
    // Read newest first: events.csv → events.0.csv → events.1.csv → events.2.csv

    String r0 = makeRotatedPath(0);
    String r1 = makeRotatedPath(1);
    String r2 = makeRotatedPath(2);
    const char* paths[] = { LOG_FILE_PATH, r0.c_str(), r1.c_str(), r2.c_str() };

    int totalLines = 0;
    int returned = 0;

    spiffsLock();
    for (int f = 0; f <= LOG_ROTATE_KEEP; f++) {
        if (!SPIFFS.exists(paths[f])) continue;

        File file = SPIFFS.open(paths[f], "r");
        if (!file) continue;

        char chunk[SPIFFS_READ_CHUNK];
        String content;
        content.reserve(512);
        while (file.available()) {
            size_t len = file.readBytes(chunk, sizeof(chunk) - 1);
            if (len == 0) break;
            chunk[len] = '\0';
            content += chunk;
        }
        file.close();

        // Split into lines and iterate
        int lineStart = 0;
        int lineEnd;
        while ((lineEnd = content.indexOf('\n', lineStart)) >= 0) {
            int len = lineEnd - lineStart;
            if (len > 0) {
                totalLines++;
                if (totalLines > offset && returned < limit) {
                    String line = content.substring(lineStart, lineEnd);
                    callback(line.c_str(), arg);
                    returned++;
                }
            }
            lineStart = lineEnd + 1;
        }

        if (returned >= limit) break;
    }
    spiffsUnlock();

    return totalLines;
}

String logFileGetAll() {
    spiffsLock();
    String result;
    result.reserve(SPIFFS_DOWNLOAD_MAX);
    String r0 = makeRotatedPath(0);
    String r1 = makeRotatedPath(1);
    String r2 = makeRotatedPath(2);
    const char* paths[] = { LOG_FILE_PATH, r0.c_str(), r1.c_str(), r2.c_str() };

    char chunk[SPIFFS_READ_CHUNK];
    for (int i = 0; i < 4; i++) {
        if (!SPIFFS.exists(paths[i])) continue;
        File f = SPIFFS.open(paths[i], "r");
        if (f) {
            while (f.available() && result.length() < SPIFFS_DOWNLOAD_MAX) {
                size_t len = f.readBytes(chunk, sizeof(chunk) - 1);
                if (len == 0) break;
                chunk[len] = '\0';
                result += chunk;
            }
            f.close();
        }
        if (result.length() >= SPIFFS_DOWNLOAD_MAX) break;
    }

    spiffsUnlock();
    return result;
}

size_t logFileSize() {
    spiffsLock();
    size_t total = 0;
    File f = SPIFFS.open(LOG_FILE_PATH, "r");
    if (f) { total += f.size(); f.close(); }

    for (int i = 0; i < LOG_ROTATE_KEEP; i++) {
        String p = makeRotatedPath(i);
        File rf = SPIFFS.open(p, "r");
        if (rf) { total += rf.size(); rf.close(); }
    }
    spiffsUnlock();
    return total;
}

void logFileClear() {
    spiffsLock();
    SPIFFS.remove(LOG_FILE_PATH);
    for (int i = 0; i < LOG_ROTATE_KEEP; i++) {
        SPIFFS.remove(makeRotatedPath(i));
    }
    spiffsUnlock();
}

int logFileCount() {
    spiffsLock();
    int count = 0;
    String r0 = makeRotatedPath(0);
    String r1 = makeRotatedPath(1);
    String r2 = makeRotatedPath(2);
    const char* paths[] = { LOG_FILE_PATH, r0.c_str(), r1.c_str(), r2.c_str() };

    char chunk[SPIFFS_READ_CHUNK];
    for (int i = 0; i < 4; i++) {
        if (!SPIFFS.exists(paths[i])) continue;
        File f = SPIFFS.open(paths[i], "r");
        if (f) {
            while (f.available()) {
                size_t len = f.readBytes(chunk, sizeof(chunk) - 1);
                if (len == 0) break;
                for (size_t j = 0; j < len; j++) {
                    if (chunk[j] == '\n') count++;
                }
            }
            f.close();
        }
    }
    spiffsUnlock();
    return count;
}
