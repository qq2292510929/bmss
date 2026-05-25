#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

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

TFT_eSPI tft = TFT_eSPI();

bool bleConnected = false;
unsigned long lastNotifyTime = 0;
uint8_t frameBuffer[320];
int framePos = 0;
bool frameStarted = false;
bool newDataAvailable = false;
NimBLEClient* pClient = nullptr;

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

float currentPowerHue = 0;
float targetPowerHue = 0;
unsigned long lastAnimationTime = 0;
int pulsePhase = 0;

class JKBMS {
public:
    NimBLERemoteCharacteristic* pChr = nullptr;
    NimBLEAdvertisedDevice* advDevice = nullptr;
    bool doConnect = false;
    bool connected = false;
    std::string targetMAC;

    JKBMS(const char* mac) : targetMAC(mac) {}
    JKBMS() = default;

    bool connectToServer();
    void handleNotification(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify);
    void writeRegister(uint8_t address, uint32_t value);
    uint8_t calculateCRC(const uint8_t data[], uint16_t len);
};

class BMSCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        DEBUG_PRINTLN("BLE connected");
        bleConnected = true;
    }
    void onDisconnect(NimBLEClient* pClient) {
        DEBUG_PRINTLN("BLE disconnected");
        bleConnected = false;
    }
};

JKBMS jkBms(BMS_MAC);

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        String devName = advertisedDevice->getName().c_str();
        String devAddr = advertisedDevice->getAddress().toString().c_str();
        DEBUG_PRINTF("Found: %s [%s]\n", devName.c_str(), devAddr.c_str());

        if (devAddr.equals(BMS_MAC) || devName.equals(BMS_NAME)) {
            DEBUG_PRINTLN("Target BMS found!");
            jkBms.advDevice = advertisedDevice;
            jkBms.doConnect = true;
            NimBLEDevice::getScan()->stop();
        }
    }
};

BMSCallbacks bmsCallbacks;
ScanCallbacks scanCallbacks;

uint8_t JKBMS::calculateCRC(const uint8_t data[], uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool JKBMS::connectToServer() {
    DEBUG_PRINTLN("Connecting to BMS...");

    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&bmsCallbacks);
    pClient->setConnectTimeout(10);

    if (!pClient->connect(advDevice)) {
        DEBUG_PRINTLN("Connection failed");
        pClient = nullptr;
        return false;
    }

    DEBUG_PRINTLN("Connected!");

    NimBLERemoteService* pSvc = pClient->getService("ffe0");
    if (pSvc) {
        pChr = pSvc->getCharacteristic("ffe1");
        if (pChr && pChr->canNotify()) {
            if (pChr->subscribe(true, std::bind(&JKBMS::handleNotification, this,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4))) {
                DEBUG_PRINTLN("Subscribed");
                delay(500);
                writeRegister(0x97, 0);
                delay(300);
                writeRegister(0x96, 0);
                return true;
            }
        }
    }
    DEBUG_PRINTLN("Service not found");
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

void JKBMS::handleNotification(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
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
        case 0x01: parseSettingsFrame(); break;
        case 0x02: parseCellFrame(); break;
        case 0x03: parseDeviceInfoFrame(); break;
    }
}

void parseCellFrame() {
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
    bmsData.cellCount = frameBuffer[114];
}

void parseDeviceInfoFrame() {
}

void calculatePowerHue() {
    float absPower = abs(bmsData.batteryPower);
    if (absPower < 100) targetPowerHue = 160;
    else if (absPower < 500) targetPowerHue = 80;
    else if (absPower < 1500) targetPowerHue = 40;
    else targetPowerHue = 0;
}

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
        color1 = COLOR_POWER_LOW; color2 = COLOR_POWER_LOW; ratio = 0;
    } else if (absPower < 500) {
        color1 = COLOR_POWER_LOW; color2 = COLOR_POWER_MED; ratio = (absPower - 100) / 400.0;
    } else if (absPower < 1500) {
        color1 = COLOR_POWER_MED; color2 = COLOR_POWER_HIGH; ratio = (absPower - 500) / 1000.0;
    } else {
        color1 = COLOR_POWER_HIGH; color2 = COLOR_POWER_MAX; ratio = min((absPower - 1500) / 1500.0, 1.0);
    }

    currentPowerHue += (targetPowerHue - currentPowerHue) * 0.1;
    return interpolateColor(color1, color2, ratio);
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
        tft.setTextFont(1);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x - 25, y - 4);
        tft.print("BLE");
    } else {
        tft.fillCircle(x, y, 8, TFT_RED);
        tft.fillCircle(x, y, 4, interpolateColor(TFT_RED, TFT_BLACK, 0.5));
        tft.setTextFont(1);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x - 25, y - 4);
        tft.print("BLE");
    }
}

void drawMainPowerDisplay() {
    int16_t cx = 160;
    int16_t cy = 90;
    uint16_t powerColor = getPowerColor();
    uint16_t cardBg = interpolateColor(powerColor, TFT_BLACK, 0.8);

    tft.fillRoundRect(cx - 110, cy - 55, 220, 110, 15, cardBg);
    drawCardBorder(cx - 110, cy - 55, 220, 110, powerColor);

    tft.setTextFont(1);
    tft.setTextColor(powerColor, cardBg);
    tft.setTextSize(1);
    tft.setCursor(cx - 36, cy - 48);
    tft.print("POWER");

    float displayPower = abs(bmsData.batteryPower);
    tft.setTextFont(7);
    tft.setTextColor(TFT_WHITE, cardBg);
    tft.setTextSize(1);
    tft.setCursor(cx - 70, cy - 20);
    tft.print(displayPower, 0);

    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, cardBg);
    tft.setTextSize(2);
    tft.setCursor(cx + 60, cy + 10);
    tft.print("W");

    tft.setTextFont(1);
    tft.setTextSize(1);
    if (bmsData.batteryPower < -10) {
        tft.setTextColor(COLOR_ACCENT, cardBg);
        tft.setCursor(cx - 24, cy + 38);
        tft.print("CHG");
    } else if (bmsData.batteryPower > 10) {
        tft.setTextColor(TFT_GREEN, cardBg);
        tft.setCursor(cx - 24, cy + 38);
        tft.print("DIS");
    } else {
        tft.setTextColor(COLOR_TEXT_DIM, cardBg);
        tft.setCursor(cx - 20, cy + 38);
        tft.print("IDLE");
    }

    int barW = 200;
    int barH = 4;
    int barX = cx - 100;
    int barY = cy + 50;
    tft.fillRoundRect(barX, barY, barW, barH, 2, interpolateColor(TFT_BLACK, COLOR_PRIMARY, 0.3));
    int fillW = min((int)(barW * displayPower / 3000.0), barW);
    if (fillW > 0) {
        tft.fillRoundRect(barX, barY, fillW, barH, 2, powerColor);
    }
}

void drawCapacityDisplay() {
    int16_t x = 20;
    int16_t y = 155;
    int16_t w = 190;
    int16_t h = 65;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 12, cardColor);
    drawCardBorder(x, y, w, h, interpolateColor(getPowerColor(), COLOR_PRIMARY, 0.5));

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 6);
    tft.print("CAPACITY");

    String remainStr = String(bmsData.remainingCapacity, 1);
    String totalStr = String(bmsData.nominalCapacity, 1);
    String capStr = remainStr + "/" + totalStr + "Ah";

    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 20);
    tft.print(capStr);

    int barWidth = w - 20;
    int barHeight = 10;
    int barX = x + 10;
    int barY = y + h - 18;
    tft.fillRoundRect(barX, barY, barWidth, barHeight, 4, interpolateColor(TFT_BLACK, COLOR_PRIMARY, 0.3));

    float usagePercent = bmsData.nominalCapacity > 0 ?
                         (1 - bmsData.remainingCapacity / bmsData.nominalCapacity) * 100 : 0;
    int fillWidth = (barWidth * usagePercent) / 100;
    if (fillWidth > 0) {
        tft.fillRoundRect(barX, barY, fillWidth, barHeight, 4, getPowerColor());
    }

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT, cardColor);
    tft.setTextSize(1);
    tft.setCursor(barX + barWidth - 25, barY - 2);
    tft.print(String(bmsData.soc) + "%");
}

void drawSOCDisplay() {
    int16_t x = 220;
    int16_t y = 155;
    int16_t w = 90;
    int16_t h = 65;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 12, cardColor);
    drawCardBorder(x, y, w, h, interpolateColor(COLOR_ACCENT, COLOR_PRIMARY, 0.3));

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 28, y + 6);
    tft.print("SOC");

    tft.setTextFont(7);
    tft.setTextColor(COLOR_ACCENT, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 15, y + 22);
    tft.print(bmsData.soc);

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT, cardColor);
    tft.setTextSize(2);
    tft.setCursor(x + 65, y + 32);
    tft.print("%");
}

void drawBatteryStatus() {
    int16_t x = 20;
    int16_t y = 230;
    int16_t w = 145;
    int16_t h = 50;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 10, cardColor);

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 5);
    tft.print("VOLTAGE");

    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 18);
    tft.print(String(bmsData.batteryVoltage, 1));
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.print("V");
}

void drawCurrentDisplay() {
    int16_t x = 175;
    int16_t y = 230;
    int16_t w = 145;
    int16_t h = 50;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 10, cardColor);

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 5);
    tft.print("CURRENT");

    tft.setTextFont(4);
    uint16_t currentColor = bmsData.chargeCurrent < 0 ? TFT_GREEN : TFT_WHITE;
    tft.setTextColor(currentColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 18);
    tft.print(String(abs(bmsData.chargeCurrent), 1));
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.print("A");
}

void drawTemperatureDisplay() {
    int16_t x = 20;
    int16_t y = 290;
    int16_t w = 100;
    int16_t h = 45;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 10, cardColor);

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 4);
    tft.print("CELL TEMP");

    float avgTemp = (bmsData.batteryTemp1 + bmsData.batteryTemp2) / 2;
    tft.setTextFont(4);
    uint16_t tempColor = avgTemp > 40 ? TFT_RED : (avgTemp > 30 ? COLOR_ACCENT : TFT_WHITE);
    tft.setTextColor(tempColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 17);
    tft.print(String(avgTemp, 0));
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print("C");
}

void drawMOSTemperatureDisplay() {
    int16_t x = 130;
    int16_t y = 290;
    int16_t w = 100;
    int16_t h = 45;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 10, cardColor);

    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 4);
    tft.print("MOS TEMP");

    tft.setTextFont(4);
    uint16_t mosColor = bmsData.mosTemp > 50 ? TFT_RED : (bmsData.mosTemp > 40 ? COLOR_ACCENT : TFT_WHITE);
    tft.setTextColor(mosColor, cardColor);
    tft.setTextSize(1);
    tft.setCursor(x + 8, y + 17);
    tft.print(String(bmsData.mosTemp, 0));
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print("C");
}

void drawStatusIndicators() {
    int16_t x = 240;
    int16_t y = 290;
    int16_t w = 80;
    int16_t h = 45;
    uint16_t cardColor = COLOR_CARD_BG;

    tft.fillRoundRect(x, y, w, h, 10, cardColor);

    tft.setTextFont(1);
    tft.setTextSize(1);
    int iy = y + 6;

    if (bmsData.chargeMOS) {
        tft.fillRect(x + 8, iy, 12, 8, TFT_GREEN);
        tft.setTextColor(TFT_WHITE, cardColor);
        tft.setCursor(x + 24, iy);
        tft.print("CHG");
    } else {
        tft.fillRect(x + 8, iy, 12, 8, interpolateColor(TFT_GREEN, TFT_BLACK, 0.5));
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setCursor(x + 24, iy);
        tft.print("CHG");
    }

    if (bmsData.dischargeMOS) {
        tft.fillRect(x + 8, iy + 12, 12, 8, COLOR_ACCENT);
        tft.setTextColor(TFT_WHITE, cardColor);
        tft.setCursor(x + 24, iy + 12);
        tft.print("DIS");
    } else {
        tft.fillRect(x + 8, iy + 12, 12, 8, interpolateColor(COLOR_ACCENT, TFT_BLACK, 0.5));
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setCursor(x + 24, iy + 12);
        tft.print("DIS");
    }

    int blink = (millis() / 300) % 2;
    if (bmsData.balancing) {
        tft.fillRect(x + 8, iy + 24, 12, 8, blink ? TFT_BLUE : interpolateColor(TFT_BLUE, TFT_BLACK, 0.5));
        tft.setTextColor(TFT_WHITE, cardColor);
        tft.setCursor(x + 24, iy + 24);
        tft.print("BAL");
    } else {
        tft.fillRect(x + 8, iy + 24, 12, 8, interpolateColor(TFT_BLUE, TFT_BLACK, 0.5));
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.setCursor(x + 24, iy + 24);
        tft.print("BAL");
    }
}

void drawHeader() {
    tft.fillRect(0, 0, 320, 30, interpolateColor(COLOR_PRIMARY, TFT_BLACK, 0.7));
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, interpolateColor(COLOR_PRIMARY, TFT_BLACK, 0.7));
    tft.setTextSize(1);
    tft.setCursor(10, 8);
    tft.print("MECHA BMS");
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
    tft.fillScreen(COLOR_BG_DARK);
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

    tft.setTextFont(2);
    tft.setTextColor(COLOR_PRIMARY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(70, 70);
    tft.print("MECHA BMS");

    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(90, 110);
    tft.print("MONITOR");

    tft.setTextColor(COLOR_TEXT_DIM, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(85, 150);
    tft.print("Connecting BLE...");
    tft.setCursor(80, 165);
    tft.print("MAC: " + String(BMS_MAC));

    tft.setTextColor(COLOR_TEXT_DIM, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(80, 230);
    tft.print("ST7789 320x240");
    tft.setCursor(85, 245);
    tft.print("JK02_32S Protocol");
}

void setup() {
    #if DEBUG_ENABLED
    Serial.begin(115200);
    #endif

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);

    #if defined(TFT_BL)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    #endif

    NimBLEDevice::init("");

    drawConnectingScreen();
}

void loop() {
    if (!bleConnected) {
        if (!jkBms.doConnect) {
            NimBLEScan* pScan = NimBLEDevice::getScan();
            pScan->setAdvertisedDeviceCallbacks(&scanCallbacks);
            pScan->setActiveScan(true);
            pScan->start(5);
        }

        if (jkBms.doConnect) {
            jkBms.doConnect = false;
            if (jkBms.connectToServer()) {
                DEBUG_PRINTLN("Connected!");
            } else {
                DEBUG_PRINTLN("Failed, retry in 5s...");
                delay(5000);
            }
        }
    } else {
        if (newDataAvailable) {
            newDataAvailable = false;
            updateDisplay();
        }

        if (millis() - lastNotifyTime > 10000) {
            bleConnected = false;
            if (pClient) {
                pClient->disconnect();
            }
        }
    }

    delay(10);
}
