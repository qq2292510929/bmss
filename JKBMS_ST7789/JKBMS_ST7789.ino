#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

// ==================== 配置区域 ====================
#define BMS_MAC "98:da:20:07:b9:00"
#define BMS_NAME "JK_BD4A24S10P"
#define PROTOCOL_VERSION "JK02_32S"
#define DEBUG_ENABLED false

#if DEBUG_ENABLED
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF(...)
#endif

// ==================== 颜色定义 - 机甲战斗风格 ====================
#define COLOR_BG_DARK     0x0A0A
#define COLOR_PRIMARY     0xF800
#define COLOR_SECONDARY   0xFA20
#define COLOR_ACCENT      0xFF00
#define COLOR_POWER_LOW   0x00F0
#define COLOR_POWER_MED   0x0FF0
#define COLOR_POWER_HIGH  0xFF00
#define COLOR_POWER_MAX   0xF800
#define COLOR_CARD_BG     0x1A1A
#define COLOR_TEXT        0xFFFF
#define COLOR_TEXT_DIM    0x8410

// ==================== 全局变量 ====================
TFT_eSPI tft = TFT_eSPI();

bool bleConnected = false;
unsigned long lastNotifyTime = 0;
uint8_t frameBuffer[320];
int framePos = 0;
bool frameStarted = false;
bool newDataAvailable = false;

// ==================== BMS数据结构 ====================
struct BMSData {
    float cellVoltages[32];
    float batteryVoltage;
    float batteryPower;
    float chargeCurrent;
    float mosTemp;
    float batteryTemp1;
    float batteryTemp2;
    int soc;
    float remainingCapacity;
    float nominalCapacity;
    int cycleCount;
    bool chargeMOS;
    bool dischargeMOS;
    bool balancing;
    int cellCount;
    float avgCellVoltage;
    float deltaCellVoltage;
} bmsData;

// ==================== 动画变量 ====================
float currentPowerHue = 0;
float targetPowerHue = 0;
unsigned long lastAnimationTime = 0;
int pulsePhase = 0;

// ==================== JKBMS类 ====================
class JKBMS {
public:
    NimBLERemoteCharacteristic* pChr = nullptr;
    const NimBLEAdvertisedDevice* advDevice = nullptr;
    bool doConnect = false;
    bool connected = false;
    std::string targetMAC;
    
    JKBMS(const std::string& mac) : targetMAC(mac) {}
    JKBMS() = default;
    
    bool connectToServer();
    void handleNotification(uint8_t* pData, size_t length);
    void writeRegister(uint8_t address, uint32_t value);
    uint8_t calculateCRC(const uint8_t data[], uint16_t len);
};

// ==================== 回调类 ====================
class BMSCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        DEBUG_PRINTLN("已连接到BMS");
        bleConnected = true;
    }
    void onDisconnect(NimBLEClient* pClient, int reason) {
        DEBUG_PRINTF("BMS断开连接，原因: %d\n", reason);
        bleConnected = false;
    }
};

// 提前声明jkBms供ScanCallbacks使用
JKBMS jkBms(BMS_MAC);

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
        String devName = advertisedDevice->getName().c_str();
        String devAddr = advertisedDevice->getAddress().toString().c_str();
        DEBUG_PRINTF("发现设备: %s [%s]\n", devName.c_str(), devAddr.c_str());
        
        if (devAddr.equals(BMS_MAC) || devName.equals(BMS_NAME)) {
            DEBUG_PRINTLN("找到目标BMS设备!");
            jkBms.advDevice = advertisedDevice;
            jkBms.doConnect = true;
            NimBLEDevice::getScan()->stop();
        }
    }
};

BMSCallbacks bmsCallbacks;
ScanCallbacks scanCallbacks;

NimBLEScan* pScan = nullptr;

// ==================== 核心函数实现 ====================

uint8_t JKBMS::calculateCRC(const uint8_t data[], uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool JKBMS::connectToServer() {
    DEBUG_PRINTLN("尝试连接到BMS...");
    
    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&bmsCallbacks, true);
        pClient->setConnectionParams(12, 12, 0, 150);
        pClient->setConnectTimeout(10);
    }
    
    if (!pClient->connect(advDevice)) {
        DEBUG_PRINTLN("连接失败");
        return false;
    }
    
    DEBUG_PRINTF("已连接: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
    
    NimBLERemoteService* pSvc = pClient->getService("ffe0");
    if (pSvc) {
        pChr = pSvc->getCharacteristic("ffe1");
        if (pChr && pChr->canNotify()) {
            if (pChr->subscribe(true, notifyCallback)) {
                DEBUG_PRINTLN("通知订阅成功");
                delay(500);
                writeRegister(0x97, 0);
                delay(300);
                writeRegister(0x96, 0);
                return true;
            }
        }
    }
    DEBUG_PRINTLN("服务未找到");
    return false;
}

void JKBMS::writeRegister(uint8_t address, uint32_t value) {
    uint8_t frame[20] = {0xAA, 0x55, 0x90, 0xEB, address, 0x00};
    frame[6] = value & 0xFF;
    frame[7] = (value >> 8) & 0xFF;
    frame[8] = (value >> 16) & 0xFF;
    frame[9] = (value >> 24) & 0xFF;
    frame[19] = calculateCRC(frame, 19);
    
    if (pChr) {
        pChr->writeValue(frame, 20);
    }
}

void notifyCallback(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
    jkBms.handleNotification(pData, length);
}

void JKBMS::handleNotification(uint8_t* pData, size_t length) {
    lastNotifyTime = millis();
    
    if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
        framePos = 0;
        frameStarted = true;
        memcpy(frameBuffer, pData, length);
        framePos = length;
    } else if (frameStarted) {
        memcpy(frameBuffer + framePos, pData, length);
        framePos += length;
        
        if (framePos >= 300) {
            frameStarted = false;
            newDataAvailable = true;
            parseFrame();
        }
    }
}

void parseFrame() {
    uint8_t frameType = frameBuffer[4];
    
    switch (frameType) {
        case 0x01:
            parseSettingsFrame();
            break;
        case 0x02:
            parseCellFrame();
            break;
        case 0x03:
            parseDeviceInfoFrame();
            break;
    }
}

void parseCellFrame() {
    DEBUG_PRINTLN("解析电芯数据帧");
    
    for (int i = 0; i < 32; i++) {
        uint16_t voltage = frameBuffer[6 + i*2] | (frameBuffer[7 + i*2] << 8);
        bmsData.cellVoltages[i] = voltage * 0.001;
    }
    
    bmsData.batteryVoltage = (frameBuffer[150] | (frameBuffer[151] << 8) | 
                              (frameBuffer[152] << 16) | (frameBuffer[153] << 24)) * 0.001;
    
    int32_t powerRaw = frameBuffer[154] | (frameBuffer[155] << 8) | 
                       (frameBuffer[156] << 16) | (frameBuffer[157] << 24);
    bmsData.batteryPower = powerRaw * 0.001;
    
    int32_t currentRaw = frameBuffer[158] | (frameBuffer[159] << 8) | 
                         (frameBuffer[160] << 16) | (frameBuffer[161] << 24);
    bmsData.chargeCurrent = currentRaw * 0.001;
    
    int16_t mosTempRaw = frameBuffer[144] | (frameBuffer[145] << 8);
    bmsData.mosTemp = mosTempRaw * 0.1;
    
    int16_t temp1Raw = frameBuffer[162] | (frameBuffer[163] << 8);
    bmsData.batteryTemp1 = temp1Raw * 0.1;
    
    int16_t temp2Raw = frameBuffer[164] | (frameBuffer[165] << 8);
    bmsData.batteryTemp2 = temp2Raw * 0.1;
    
    bmsData.soc = frameBuffer[173];
    
    bmsData.remainingCapacity = (frameBuffer[174] | (frameBuffer[175] << 8) | 
                                (frameBuffer[176] << 16) | (frameBuffer[177] << 24)) * 0.001;
    
    bmsData.nominalCapacity = (frameBuffer[178] | (frameBuffer[179] << 8) | 
                              (frameBuffer[180] << 16) | (frameBuffer[181] << 24)) * 0.001;
    
    bmsData.chargeMOS = frameBuffer[198];
    bmsData.dischargeMOS = frameBuffer[199];
    bmsData.balancing = frameBuffer[201];
    
    bmsData.avgCellVoltage = (frameBuffer[74] | (frameBuffer[75] << 8)) * 0.001;
    bmsData.deltaCellVoltage = (frameBuffer[76] | (frameBuffer[77] << 8)) * 0.001;
    
    calculatePowerHue();
}

void parseSettingsFrame() {
    DEBUG_PRINTLN("解析设置帧");
    bmsData.cellCount = frameBuffer[114];
}

void parseDeviceInfoFrame() {
    DEBUG_PRINTLN("解析设备信息帧");
}

void calculatePowerHue() {
    float absPower = abs(bmsData.batteryPower);
    
    if (absPower < 100) {
        targetPowerHue = 160;
    } else if (absPower < 500) {
        targetPowerHue = 80;
    } else if (absPower < 1500) {
        targetPowerHue = 40;
    } else {
        targetPowerHue = 0;
    }
}

// ==================== UI绘制函数 ====================

uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    return (r << 11) | (g << 5) | b;
}

uint16_t getPowerColor() {
    float absPower = abs(bmsData.batteryPower);
    float ratio;
    uint16_t color1, color2;
    
    if (absPower < 100) {
        color1 = COLOR_POWER_LOW;
        color2 = COLOR_POWER_LOW;
        ratio = 0;
    } else if (absPower < 500) {
        color1 = COLOR_POWER_LOW;
        color2 = COLOR_POWER_MED;
        ratio = (absPower - 100) / 400.0;
    } else if (absPower < 1500) {
        color1 = COLOR_POWER_MED;
        color2 = COLOR_POWER_HIGH;
        ratio = (absPower - 500) / 1000.0;
    } else {
        color1 = COLOR_POWER_HIGH;
        color2 = COLOR_POWER_MAX;
        ratio = min((absPower - 1500) / 1500.0, 1.0);
    }
    
    currentPowerHue += (targetPowerHue - currentPowerHue) * 0.1;
    return interpolateColor(color1, color2, ratio);
}

void drawRoundedRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, r, color);
}

void drawCardBorder(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    uint16_t glowColor = interpolateColor(color, TFT_BLACK, 0.7);
    tft.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 8, glowColor);
    tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 6, color);
}

void drawCornerDecorations() {
    uint16_t decorColor = interpolateColor(getPowerColor(), TFT_WHITE, 0.3);
    tft.fillTriangle(0, 0, 15, 0, 0, 15, decorColor);
    tft.fillTriangle(319, 0, 304, 0, 319, 15, decorColor);
    tft.fillTriangle(0, 239, 15, 239, 0, 224, decorColor);
    tft.fillTriangle(319, 239, 304, 239, 319, 224, decorColor);
}

void drawBleStatus() {
    int16_t x = 290;
    int16_t y = 10;
    
    if (bleConnected) {
        uint16_t blinkColor = (millis() / 500) % 2 == 0 ? TFT_GREEN : interpolateColor(TFT_GREEN, TFT_BLACK, 0.5);
        tft.fillCircle(x, y, 8, blinkColor);
        tft.fillCircle(x, y, 4, TFT_WHITE);
        
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x - 25, y - 4);
        tft.println("BLE");
    } else {
        tft.fillCircle(x, y, 8, TFT_RED);
        tft.fillCircle(x, y, 4, interpolateColor(TFT_RED, TFT_BLACK, 0.5));
        
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x - 25, y - 4);
        tft.println("BLE");
    }
}

void drawMainPowerDisplay() {
    int16_t centerX = 160;
    int16_t centerY = 85;
    
    uint16_t powerColor = getPowerColor();
    
    drawRoundedRect(centerX - 100, centerY - 50, 200, 100, 15, interpolateColor(powerColor, TFT_BLACK, 0.8));
    drawCardBorder(centerX - 100, centerY - 50, 200, 100, powerColor);
    
    tft.setTextColor(powerColor, interpolateColor(powerColor, TFT_BLACK, 0.8));
    tft.setTextSize(1);
    tft.setCursor(centerX - 40, centerY - 40);
    tft.println("实时放电功率");
    
    float displayPower = abs(bmsData.batteryPower);
    String powerStr = String(displayPower, 1);
    
    tft.setFreeFont(&Orbitron_Medium_32);
    tft.setTextColor(TFT_WHITE, interpolateColor(powerColor, TFT_BLACK, 0.8));
    tft.setTextSize(1);
    tft.setCursor(centerX - 60, centerY + 5);
    tft.println(powerStr);
    
    tft.setFreeFont(&Orbitron_Medium_Bold_24);
    tft.setTextColor(TFT_WHITE, interpolateColor(powerColor, TFT_BLACK, 0.8));
    tft.setTextSize(1);
    tft.setCursor(centerX + 30, centerY + 15);
    tft.println("W");
    
    if (bmsData.batteryPower < -10) {
        tft.setTextColor(COLOR_ACCENT, interpolateColor(powerColor, TFT_BLACK, 0.8));
        tft.setTextSize(1);
        tft.setCursor(centerX - 30, centerY + 35);
        tft.println("充电中");
    } else if (bmsData.batteryPower > 10) {
        tft.setTextColor(TFT_GREEN, interpolateColor(powerColor, TFT_BLACK, 0.8));
        tft.setTextSize(1);
        tft.setCursor(centerX - 30, centerY + 35);
        tft.println("放电中");
    }
}

void drawCapacityDisplay() {
    int16_t x = 20;
    int16_t y = 145;
    int16_t w = 180;
    int16_t h = 70;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 12, cardColor);
    drawCardBorder(x, y, w, h, interpolateColor(getPowerColor(), COLOR_PRIMARY, 0.5));
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 10);
    tft.println("已用 / 总容量");
    
    String remainStr = String(bmsData.remainingCapacity, 2);
    String totalStr = String(bmsData.nominalCapacity, 2);
    String capacityStr = remainStr + " / " + totalStr + " Ah";
    
    tft.setFreeFont(&Orbitron_Medium_Bold_20);
    tft.setTextColor(TFT_WHITE, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 30);
    tft.println(capacityStr);
    
    int barWidth = w - 40;
    int barHeight = 12;
    int barX = x + 20;
    int barY = y + h - 22;
    
    tft.fillRoundRect(barX, barY, barWidth, barHeight, 4, interpolateColor(TFT_BLACK, COLOR_PRIMARY, 0.3));
    
    float usagePercent = bmsData.nominalCapacity > 0 ? 
                         (1 - bmsData.remainingCapacity / bmsData.nominalCapacity) * 100 : 0;
    int fillWidth = (barWidth * usagePercent) / 100;
    if (fillWidth > 0) {
        tft.fillRoundRect(barX, barY, fillWidth, barHeight, 4, getPowerColor());
    }
    
    tft.setTextColor(COLOR_TEXT, cardColor);
    tft.setTextSize(1);
    tft.setCursor(barX + barWidth - 35, barY - 1);
    tft.println(String(bmsData.soc) + "%");
}

void drawSOCDisplay() {
    int16_t x = 210;
    int16_t y = 145;
    int16_t w = 100;
    int16_t h = 70;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 12, cardColor);
    drawCardBorder(x, y, w, h, interpolateColor(COLOR_ACCENT, COLOR_PRIMARY, 0.3));
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 25, y + 10);
    tft.println("电量");
    
    String socStr = String(bmsData.soc);
    
    tft.setFreeFont(&Orbitron_Medium_Bold_32);
    tft.setTextColor(COLOR_ACCENT, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 25, y + 25);
    tft.println(socStr);
    
    tft.setFreeFont(&Orbitron_Medium_16);
    tft.setTextColor(COLOR_TEXT, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 60, y + 35);
    tft.println("%");
}

void drawBatteryStatus() {
    int16_t x = 20;
    int16_t y = 225;
    int16_t w = 140;
    int16_t h = 50;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 10, cardColor);
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 8);
    tft.println("电池电压");
    
    tft.setFreeFont(&Orbitron_Medium_Bold_24);
    tft.setTextColor(TFT_WHITE, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 25);
    tft.println(String(bmsData.batteryVoltage, 2) + "V");
}

void drawCurrentDisplay() {
    int16_t x = 170;
    int16_t y = 225;
    int16_t w = 140;
    int16_t h = 50;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 10, cardColor);
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 8);
    tft.println("充放电流");
    
    tft.setFreeFont(&Orbitron_Medium_Bold_24);
    uint16_t currentColor = bmsData.chargeCurrent < 0 ? TFT_GREEN : TFT_WHITE;
    tft.setTextColor(currentColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 25);
    tft.println(String(abs(bmsData.chargeCurrent), 2) + "A");
}

void drawTemperatureDisplay() {
    int16_t x = 20;
    int16_t y = 285;
    int16_t w = 95;
    int16_t h = 45;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 10, cardColor);
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 6);
    tft.println("电芯温度");
    
    tft.setFreeFont(&Orbitron_Medium_Bold_20);
    float avgTemp = (bmsData.batteryTemp1 + bmsData.batteryTemp2) / 2;
    uint16_t tempColor = avgTemp > 40 ? TFT_RED : (avgTemp > 30 ? COLOR_ACCENT : TFT_WHITE);
    tft.setTextColor(tempColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 22);
    tft.println(String(avgTemp, 1) + "C");
}

void drawMOSTemperatureDisplay() {
    int16_t x = 125;
    int16_t y = 285;
    int16_t w = 95;
    int16_t h = 45;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 10, cardColor);
    
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 6);
    tft.println("MOS温度");
    
    tft.setFreeFont(&Orbitron_Medium_Bold_20);
    uint16_t mosColor = bmsData.mosTemp > 50 ? TFT_RED : (bmsData.mosTemp > 40 ? COLOR_ACCENT : TFT_WHITE);
    tft.setTextColor(mosColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 22);
    tft.println(String(bmsData.mosTemp, 1) + "C");
}

void drawStatusIndicators() {
    int16_t x = 230;
    int16_t y = 285;
    int16_t w = 80;
    int16_t h = 45;
    
    uint16_t cardColor = COLOR_CARD_BG;
    drawRoundedRect(x, y, w, h, 10, cardColor);
    
    int iconY = y + 8;
    
    if (bmsData.chargeMOS) {
        tft.fillRect(x + 10, iconY, 15, 10, TFT_GREEN);
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setTextSize(1);
        tft.setCursor(x + 30, iconY);
        tft.println("充");
    }
    
    if (bmsData.dischargeMOS) {
        tft.fillRect(x + 10, iconY + 15, 15, 10, COLOR_ACCENT);
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setTextSize(1);
        tft.setCursor(x + 30, iconY + 15);
        tft.println("放");
    }
    
    if (bmsData.balancing) {
        int blink = (millis() / 300) % 2;
        tft.fillRect(x + 10, iconY + 30, 15, 8, blink ? TFT_BLUE : interpolateColor(TFT_BLUE, TFT_BLACK, 0.5));
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setTextSize(1);
        tft.setCursor(x + 30, iconY + 30);
        tft.println("均");
    }
}

void drawHeader() {
    tft.fillRect(0, 0, 320, 30, interpolateColor(COLOR_PRIMARY, TFT_BLACK, 0.7));
    
    tft.setTextColor(TFT_WHITE, interpolateColor(COLOR_PRIMARY, TFT_BLACK, 0.7));
    tft.setTextSize(2);
    tft.setCursor(60, 8);
    tft.println("机甲战士 BMS");
    
    drawBleStatus();
}

void drawAnimatedEffects() {
    unsigned long currentTime = millis();
    if (currentTime - lastAnimationTime > 50) {
        lastAnimationTime = currentTime;
        pulsePhase = (pulsePhase + 1) % 360;
    }
    
    if (bleConnected) {
        int glowIntensity = (sin(pulsePhase * 0.05) + 1) * 0.3;
        uint16_t glowColor = interpolateColor(getPowerColor(), TFT_WHITE, glowIntensity);
        
        for (int i = 0; i < 3; i++) {
            int y = 35 + i * 65;
            if (y < 200) {
                tft.drawLine(0, y, 5, y, interpolateColor(glowColor, TFT_BLACK, 0.8));
                tft.drawLine(314, y, 319, y, interpolateColor(glowColor, TFT_BLACK, 0.8));
            }
        }
    }
}

void updateDisplay() {
    tft.setTextDatum(TL_DATUM);
    
    drawAnimatedEffects();
    drawCornerDecorations();
    drawHeader();
    drawMainPowerDisplay();
    drawCapacityDisplay();
    drawSOCDisplay();
    drawBatteryStatus();
    drawCurrentDisplay();
    drawTemperatureDisplay();
    drawMOSTemperatureDisplay();
    drawStatusIndicators();
}

void drawConnectingScreen() {
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(COLOR_PRIMARY, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(80, 80);
    tft.println("机甲战士");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(100, 120);
    tft.println("BMS 监控");
    
    tft.setTextColor(COLOR_TEXT_DIM, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(90, 160);
    tft.println("正在连接蓝牙...");
    tft.setCursor(85, 175);
    tft.println("MAC: " + String(BMS_MAC));
    
    int loadingX = 160;
    int loadingY = 200;
    int loadingR = 30;
    
    int angle = (millis() / 20) % 360;
    for (int i = 0; i < 12; i++) {
        int segmentAngle = (angle + i * 30) * PI / 180;
        int dotX = loadingX + cos(segmentAngle) * loadingR;
        int dotY = loadingY + sin(segmentAngle) * loadingR;
        int brightness = map(i, 0, 12, 50, 255);
        tft.fillCircle(dotX, dotY, 3, interpolateColor(COLOR_PRIMARY, TFT_BLACK, 1 - brightness / 255.0));
    }
    
    tft.setTextColor(COLOR_TEXT_DIM, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(70, 240);
    tft.println("ST7789 2.8'' 横屏显示");
    tft.setCursor(90, 260);
    tft.println("JK02_32S 协议解析");
}

// ==================== 主程序 ====================

void setup() {
    #if DEBUG_ENABLED
    Serial.begin(115200);
    #endif
    
    DEBUG_PRINTLN("初始化TFT屏幕...");
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
    
    #if defined(TFT_BL)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    #endif
    
    DEBUG_PRINTLN("初始化BLE...");
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    drawConnectingScreen();
}

void loop() {
    if (!bleConnected) {
        if (!jkBms.doConnect) {
            DEBUG_PRINTLN("开始扫描BMS...");
            pScan = NimBLEDevice::getScan();
            pScan->setScanCallbacks(&scanCallbacks);
            pScan->setActiveScan(true);
            pScan->setInterval(100);
            pScan->setWindow(50);
            pScan->start(5, false);
        }
        
        if (jkBms.doConnect) {
            jkBms.doConnect = false;
            if (jkBms.connectToServer()) {
                DEBUG_PRINTLN("连接成功!");
            } else {
                DEBUG_PRINTLN("连接失败，5秒后重试...");
                delay(5000);
            }
        }
    } else {
        if (newDataAvailable) {
            newDataAvailable = false;
            updateDisplay();
        }
        
        if (millis() - lastNotifyTime > 10000) {
            DEBUG_PRINTLN("BMS连接超时");
            bleConnected = false;
            NimBLEDevice::getClientByPeerAddress(jkBms.advDevice->getAddress())->disconnect();
        }
    }
    
    delay(10);
}
