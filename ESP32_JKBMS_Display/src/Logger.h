#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARNING = 2,
    LOG_ERROR = 3
};

class Logger {
public:
    static void begin(uint32_t baudRate);
    static void setLevel(LogLevel level);
    static void debug(const char* message);
    static void info(const char* message);
    static void warning(const char* message);
    static void error(const char* message);
    static void debug(const String& message);
    static void info(const String& message);
    static void warning(const String& message);
    static void error(const String& message);
    
private:
    static LogLevel currentLevel;
    static const char* levelToString(LogLevel level);
    static uint32_t lastLogTime;
};

#endif
