#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#define LOG_INFO(msg) Logger::info(msg)
#define LOG_ERROR(msg) Logger::error(msg)
#define LOG_DEBUG(msg) Logger::debug(msg)

class Logger {
public:
    static void init(unsigned long baud = 115200);
    static void info(const char* msg);
    static void error(const char* msg);
    static void debug(const char* msg);
    
    static void info(const String& msg);
    static void error(const String& msg);
    static void debug(const String& msg);
    
    static void infof(const char* format, ...);
    static void errorf(const char* format, ...);
    static void debugf(const char* format, ...);

private:
    static const char* timestamp();
    static bool initialized;
};

#endif // LOGGER_H