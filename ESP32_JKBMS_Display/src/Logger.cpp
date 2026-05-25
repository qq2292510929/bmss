#include "Logger.h"

LogLevel Logger::currentLevel = LOG_INFO;
uint32_t Logger::lastLogTime = 0;

void Logger::begin(uint32_t baudRate) {
    Serial.begin(baudRate);
    while (!Serial) {
        delay(10);
    }
}

void Logger::setLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::debug(const char* message) {
    if (currentLevel <= LOG_DEBUG) {
        uint32_t now = millis();
        Serial.print("[");
        Serial.print(now);
        Serial.print("] [DEBUG] ");
        Serial.println(message);
    }
}

void Logger::info(const char* message) {
    if (currentLevel <= LOG_INFO) {
        uint32_t now = millis();
        Serial.print("[");
        Serial.print(now);
        Serial.print("] [INFO] ");
        Serial.println(message);
    }
}

void Logger::warning(const char* message) {
    if (currentLevel <= LOG_WARNING) {
        uint32_t now = millis();
        Serial.print("[");
        Serial.print(now);
        Serial.print("] [WARNING] ");
        Serial.println(message);
    }
}

void Logger::error(const char* message) {
    if (currentLevel <= LOG_ERROR) {
        uint32_t now = millis();
        Serial.print("[");
        Serial.print(now);
        Serial.print("] [ERROR] ");
        Serial.println(message);
    }
}

void Logger::debug(const String& message) {
    debug(message.c_str());
}

void Logger::info(const String& message) {
    info(message.c_str());
}

void Logger::warning(const String& message) {
    warning(message.c_str());
}

void Logger::error(const String& message) {
    error(message.c_str());
}
