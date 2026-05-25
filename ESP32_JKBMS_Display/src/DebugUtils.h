#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h>
#include <JKBMS_Protocol.h>

class DebugUtils {
public:
    static void printBMSData(JKBMSData* data);
    static void printBMSDataCompact(JKBMSData* data);
    static void printHexDump(const uint8_t* data, size_t len);
    static void printConnectionStatus(bool connected);
    static void printPerformanceStats();
    static void drawDebugOverlay(JKBMSData* data, bool btConnected);
    
private:
    static unsigned long lastPrintTime;
    static unsigned long frameCount;
    static unsigned long lastFrameTime;
    static unsigned long minFrameTime;
    static unsigned long maxFrameTime;
};

#endif
