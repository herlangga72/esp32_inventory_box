#include "Logger.h"
#include <stdarg.h>

bool Logger::initialized = false;

void Logger::init(unsigned long baud) {
    Serial.begin(baud);
    initialized = true;
}

const char* Logger::timestamp() {
    static char buf[12];
    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu.%03lu",
        hr % 24, min % 60, sec % 60, ms % 1000);
    
    return buf;
}

void Logger::info(const char* msg) {
    if (!initialized) Serial.begin(115200);
    Serial.printf("[INFO] %s %s\n", timestamp(), msg);
}

void Logger::error(const char* msg) {
    if (!initialized) Serial.begin(115200);
    Serial.printf("[ERROR] %s %s\n", timestamp(), msg);
}

void Logger::debug(const char* msg) {
    #ifdef DEBUG
    if (!initialized) Serial.begin(115200);
    Serial.printf("[DEBUG] %s %s\n", timestamp(), msg);
    #endif
}

void Logger::info(const String& msg) {
    info(msg.c_str());
}

void Logger::error(const String& msg) {
    error(msg.c_str());
}

void Logger::debug(const String& msg) {
    debug(msg.c_str());
}

void Logger::infof(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    info(buf);
}

void Logger::errorf(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    error(buf);
}

void Logger::debugf(const char* format, ...) {
    #ifdef DEBUG
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    debug(buf);
    #endif
}