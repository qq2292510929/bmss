#include "DebugUtils.h"

unsigned long DebugUtils::lastPrintTime = 0;
unsigned long DebugUtils::frameCount = 0;
unsigned long DebugUtils::lastFrameTime = 0;
unsigned long DebugUtils::minFrameTime = 999999;
unsigned long DebugUtils::maxFrameTime = 0;

void DebugUtils::printBMSData(JKBMSData* data) {
    if (data == nullptr) {
        Serial.println("BMS Data: NULL");
        return;
    }
    
    Serial.println("\n========== BMS 数据详情 ==========");
    Serial.printf("电压: %.2f V\n", data->totalVoltage);
    Serial.printf("电流: %.2f A\n", data->current);
    Serial.printf("功率: %.2f W\n", data->power);
    Serial.printf("SOC: %d %%\n", (int)data->soc);
    Serial.printf("容量剩余: %.2f Ah\n", data->capacityRemaining);
    Serial.printf("容量总计: %.2f Ah\n", data->capacityTotal);
    Serial.printf("循环次数: %d\n", data->cycleCount);
    Serial.printf("电芯数量: %d\n", data->cellCount);
    Serial.printf("温度1: %d °C\n", data->temperature1);
    Serial.printf("温度2: %d °C\n", data->temperature2);
    Serial.printf("充电状态: %s\n", data->chargeStatus ? "是" : "否");
    Serial.printf("放电状态: %s\n", data->dischargeStatus ? "是" : "否");
    Serial.printf("保护标志: 0x%04X\n", data->protectionFlags);
    
    Serial.println("\n电芯电压:");
    for (int i = 0; i < data->cellCount && i < 32; i++) {
        Serial.printf("  Cell[%02d]: %.3f V\n", i + 1, data->cellVoltages[i]);
    }
    Serial.println("=================================\n");
}

void DebugUtils::printBMSDataCompact(JKBMSData* data) {
    if (data == nullptr) {
        Serial.print("[NULL] ");
        return;
    }
    
    unsigned long now = millis();
    if (now - lastPrintTime > 1000) {
        Serial.printf("[BMS] V=%.1fV I=%.1fA P=%.0fW SOC=%d%% T1=%d°C T2=%d°C\n",
                     data->totalVoltage,
                     data->current,
                     data->power,
                     (int)data->soc,
                     data->temperature1,
                     data->temperature2);
        lastPrintTime = now;
    }
}

void DebugUtils::printHexDump(const uint8_t* data, size_t len) {
    Serial.println("\n========== HEX Dump ==========");
    Serial.printf("长度: %d bytes\n", len);
    
    for (size_t i = 0; i < len; i += 16) {
        Serial.printf("%04X: ", i);
        
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            Serial.printf("%02X ", data[i + j]);
        }
        
        Serial.print(" | ");
        
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = data[i + j];
            if (c >= 32 && c < 127) {
                Serial.print((char)c);
            } else {
                Serial.print('.');
            }
        }
        
        Serial.println();
    }
    Serial.println("============================\n");
}

void DebugUtils::printConnectionStatus(bool connected) {
    static bool lastStatus = false;
    
    if (connected != lastStatus) {
        if (connected) {
            Serial.println("✓ 蓝牙已连接");
        } else {
            Serial.println("✗ 蓝牙连接断开");
        }
        lastStatus = connected;
    }
}

void DebugUtils::printPerformanceStats() {
    unsigned long now = millis();
    unsigned long frameTime = now - lastFrameTime;
    
    if (frameTime < minFrameTime) minFrameTime = frameTime;
    if (frameTime > maxFrameTime) maxFrameTime = frameTime;
    
    frameCount++;
    lastFrameTime = now;
    
    if (now - lastPrintTime > 5000) {
        Serial.println("\n========== 性能统计 ==========");
        Serial.printf("帧数: %lu\n", frameCount);
        Serial.printf("帧率: %.1f FPS\n", frameCount * 1000.0 / (now - lastPrintTime));
        Serial.printf("最小帧时间: %lu ms\n", minFrameTime);
        Serial.printf("最大帧时间: %lu ms\n", maxFrameTime);
        Serial.printf("平均帧时间: %.1f ms\n", (now - lastPrintTime) / (float)frameCount);
        Serial.println("=============================\n");
        
        lastPrintTime = now;
        frameCount = 0;
        minFrameTime = 999999;
        maxFrameTime = 0;
    }
}

void DebugUtils::drawDebugOverlay(JKBMSData* data, bool btConnected) {
    #ifdef ENABLE_DEBUG_OVERLAY
    if (!display) return;
    
    display->setTextDatum(TL_DATUM);
    display->setTextColor(TFT_WHITE, TFT_BLACK);
    display->setFreeFont(&NotoSansBold6);
    
    char debugStr[64];
    unsigned long fps = frameCount > 0 ? 1000 / ((millis() - lastFrameTime) + 1) : 0;
    
    sprintf(debugStr, "FPS:%lu MEM:%d", 
            fps, 
            ESP.getFreeHeap());
    
    display->drawString(debugStr, 5, 5);
    
    if (data) {
        sprintf(debugStr, "RSSI:%d", lastRSSI);
        display->drawString(debugStr, 5, 15);
    }
    #endif
}
