#!/bin/bash

# ===========================================
# ESP32 JK BMS 一键安装脚本 - 完整版
# ===========================================

set -e

echo "=========================================="
echo "ESP32 JK BMS 显示系统 - 一键安装"
echo "=========================================="
echo ""

# 检查命令
if ! command -v base64 &> /dev/null; then
    echo "错误: 需要 base64 命令"
    exit 1
fi

# 创建目录
PROJECT_DIR="ESP32_JKBMS_Display"
mkdir -p "$PROJECT_DIR/src" "$PROJECT_DIR/lib/TFT_eSPI" "$PROJECT_DIR/docs"

# 文件内容 (Base64编码)

# platformio.ini
echo "生成 platformio.ini..."
cat > "$PROJECT_DIR/platformio.ini" << 'EOF'
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
lib_deps = 
    bodmer/TFT_eSPI@^2.5.0
    h2zero/NimBLE-Arduino@^1.4.1
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    -DUSER_SETUP_LOADED=1
    -DST7789_2_DRIVER
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
board_build.partitions = default.csv
EOF

# src/main.cpp
echo "生成 src/main.cpp..."
cat > "$PROJECT_DIR/src/main.cpp" << 'EOF'
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <esp_bt_main.h>
#include <esp_bluedroid.h>
#include "JKBMS_Protocol.h"
#include "JKBMS_Bluetooth.h"
#include "Battle_UI.h"

#define BT_DEVICE_NAME "JK_BD4A24S10P"
#define BT_MAC_ADDRESS "98:da:20:07:b9:00"

TFT_eSPI tft = TFT_eSPI(240, 320);
JKBMSBluetooth bmsBT;
BattleUI battleUI(&tft);
JKBMSData bmsData;
bool bluetoothConnected = false;
bool displayInitialized = false;
unsigned long lastScreenUpdate = 0;

void initDisplay() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    battleUI.begin();
    displayInitialized = true;
    Serial.println("Display initialized!");
}

void initBluetooth() {
    if (!btStart() || esp_bluedroid_init() != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
        Serial.println("Bluetooth init failed!");
        return;
    }
    Serial.println("Bluetooth initialized!");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32 JK BMS Display v1.0");
    initDisplay();
    initBluetooth();
    bluetoothConnected = bmsBT.connectToBMS(BT_DEVICE_NAME, BT_MAC_ADDRESS);
    if (bluetoothConnected) {
        Serial.println("Connected to JK BMS!");
    }
}

void loop() {
    if (millis() - lastScreenUpdate >= 100) {
        if (bmsBT.hasNewData()) {
            bmsData = bmsBT.getBMSData();
        }
        if (displayInitialized) {
            battleUI.update(bluetoothConnected ? &bmsData : nullptr, bluetoothConnected);
        }
        lastScreenUpdate = millis();
    }
    delay(10);
}
EOF

# src/JKBMS_Protocol.h
echo "生成 src/JKBMS_Protocol.h..."
cat > "$PROJECT_DIR/src/JKBMS_Protocol.h" << 'EOF'
#ifndef JKBMS_PROTOCOL_H
#define JKBMS_PROTOCOL_H
#include <stdint.h>
#include <Arduino.h>

#define JK_FRAME_HEADER_0 0x4A
#define JK_FRAME_HEADER_1 0x4B

typedef struct {
    float totalVoltage;
    float current;
    float capacityRemaining;
    float capacityTotal;
    uint16_t cycleCount;
    uint8_t cellCount;
    int16_t temperature1;
    int16_t temperature2;
    float cellVoltages[32];
    uint8_t soc;
    float power;
} JKBMSData;

class JKBMSProtocol {
public:
    JKBMSProtocol();
    bool parseFrame(const uint8_t* data, size_t len);
    JKBMSData getData();
    bool isDataValid();
private:
    JKBMSData data;
    bool dataValid;
    uint16_t readUint16BE(const uint8_t* ptr);
    int16_t readInt16BE(const uint8_t* ptr);
};
#endif
EOF

# src/JKBMS_Protocol.cpp
echo "生成 src/JKBMS_Protocol.cpp..."
cat > "$PROJECT_DIR/src/JKBMS_Protocol.cpp" << 'EOF'
#include "JKBMS_Protocol.h"

JKBMSProtocol::JKBMSProtocol() {
    memset(&data, 0, sizeof(JKBMSData));
    dataValid = false;
}

uint16_t JKBMSProtocol::readUint16BE(const uint8_t* ptr) {
    return ((uint16_t)ptr[0] << 8) | ptr[1];
}

int16_t JKBMSProtocol::readInt16BE(const uint8_t* ptr) {
    return ((int16_t)ptr[0] << 8) | ptr[1];
}

bool JKBMSProtocol::parseFrame(const uint8_t* frameData, size_t len) {
    if (len < 50 || frameData[0] != JK_FRAME_HEADER_0 || frameData[1] != JK_FRAME_HEADER_1) {
        return false;
    }
    
    uint16_t dataLen = readUint16BE(&frameData[4]);
    if (dataLen > len - 8) return false;
    
    uint8_t frameType = frameData[6];
    if (frameType == 0x03) {
        data.totalVoltage = readUint16BE(&frameData[20]) / 100.0f;
        data.current = readInt16BE(&frameData[22]) / 100.0f;
        data.capacityRemaining = readUint16BE(&frameData[24]) / 100.0f;
        data.capacityTotal = readUint16BE(&frameData[26]) / 100.0f;
        data.cycleCount = readUint16BE(&frameData[28]);
        data.temperature1 = readInt16BE(&frameData[35]) - 2731;
        data.temperature2 = readInt16BE(&frameData[37]) - 2731;
        data.cellCount = frameData[45];
        
        for (int i = 0; i < data.cellCount && i < 32; i++) {
            data.cellVoltages[i] = readUint16BE(&frameData[47 + i * 2]) / 1000.0f;
        }
        
        if (data.capacityTotal > 0) {
            data.soc = (data.capacityRemaining / data.capacityTotal) * 100.0f;
        }
        data.power = data.totalVoltage * data.current;
        dataValid = true;
        return true;
    }
    return false;
}

JKBMSData JKBMSProtocol::getData() { return data; }
bool JKBMSProtocol::isDataValid() { return dataValid; }
EOF

# src/JKBMS_Bluetooth.h
echo "生成 src/JKBMS_Bluetooth.h..."
cat > "$PROJECT_DIR/src/JKBMS_Bluetooth.h" << 'EOF'
#ifndef JKBMS_BLUETOOTH_H
#define JKBMS_BLUETOOTH_H
#include <BLEDevice.h>
#include <BLEClient.h>
#include "JKBMS_Protocol.h"

#define JK_SERVICE_UUID "FFE0"
#define JK_CHAR_UUID "FFE1"

class JKBMSBluetooth : public BLEClientCallbacks {
public:
    JKBMSBluetooth();
    bool connectToBMS(const char* deviceName, const char* macAddress);
    void disconnect();
    bool isConnected();
    bool hasNewData();
    JKBMSData getBMSData();
private:
    bool connected;
    bool newDataAvailable;
    JKBMSProtocol protocol;
    static void notifyCallback(BLECharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify);
    class MyClientCallbacks : public BLEClientCallbacks {
        void onConnect(BLEClient* pclient);
        void onDisconnect(BLEClient* pclient);
    };
};
#endif
EOF

# src/JKBMS_Bluetooth.cpp
echo "生成 src/JKBMS_Bluetooth.cpp..."
cat > "$PROJECT_DIR/src/JKBMS_Bluetooth.cpp" << 'EOF'
#include "JKBMS_Bluetooth.h"

JKBMSBluetooth* globalBTClient = nullptr;

JKBMSBluetooth::JKBMSBluetooth() {
    connected = false;
    newDataAvailable = false;
    globalBTClient = this;
}

bool JKBMSBluetooth::connectToBMS(const char* deviceName, const char* macAddress) {
    Serial.println("Starting BLE scan...");
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    BLEScanResults* foundDevices = pBLEScan->start(10, false);
    
    int devicesFound = foundDevices->getCount();
    Serial.print("Devices found: ");
    Serial.println(devicesFound);
    
    BLEAdvertisedDevice* targetDevice = nullptr;
    for (int i = 0; i < devicesFound; i++) {
        BLEAdvertisedDevice device = foundDevices->getDevice(i);
        String name = device.getName().c_str();
        Serial.print("Found: ");
        Serial.println(name);
        if (name == String(deviceName)) {
            targetDevice = new BLEAdvertisedDevice(device);
            break;
        }
    }
    pBLEScan->clearResults();
    
    if (targetDevice == nullptr) {
        Serial.println("Target BMS not found!");
        return false;
    }
    
    BLEClient* pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallbacks());
    pClient->connect(targetDevice);
    
    BLERemoteService* pRemoteService = pClient->getService(JK_SERVICE_UUID);
    if (pRemoteService == nullptr) {
        Serial.println("Failed to find JK service");
        pClient->disconnect();
        return false;
    }
    
    BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(JK_CHAR_UUID);
    if (pRemoteCharacteristic && pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }
    
    connected = true;
    Serial.println("Connected to JK BMS!");
    return true;
}

void JKBMSBluetooth::notifyCallback(BLECharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (globalBTClient) {
        if (globalBTClient->protocol.parseFrame(pData, length)) {
            globalBTClient->newDataAvailable = true;
        }
    }
}

void JKBMSBluetooth::disconnect() {
    connected = false;
    BLEDevice::deinit();
}

bool JKBMSBluetooth::isConnected() { return connected; }
bool JKBMSBluetooth::hasNewData() { return newDataAvailable; }
JKBMSData JKBMSBluetooth::getBMSData() {
    newDataAvailable = false;
    return protocol.getData();
}

void JKBMSBluetooth::MyClientCallbacks::onConnect(BLEClient* pclient) {
    Serial.println("BLE Connected");
}

void JKBMSBluetooth::MyClientCallbacks::onDisconnect(BLEClient* pclient) {
    globalBTClient->connected = false;
    Serial.println("BLE Disconnected");
}
EOF

# src/Battle_UI.h
echo "生成 src/Battle_UI.h..."
cat > "$PROJECT_DIR/src/Battle_UI.h" << 'EOF'
#ifndef BATTLE_UI_H
#define BATTLE_UI_H
#include <TFT_eSPI.h>
#include "JKBMS_Protocol.h"

#define BATTLE_COLOR_LOW 0x001F
#define BATTLE_COLOR_MED 0x07E0
#define BATTLE_COLOR_HIGH 0xF800
#define BATTLE_COLOR_MAX 0xFFE0

class BattleUI {
public:
    BattleUI(TFT_eSPI* tft);
    void begin();
    void update(JKBMSData* bmsData, bool bluetoothConnected);
    void drawBackground();
    void drawPowerGauge(float power);
    void drawCapacity(float remaining, float total);
    void drawTemperatures(int16_t temp1, int16_t temp2);
    void drawBluetoothStatus(bool connected);
    void drawTitle();
    void drawSOC(uint8_t soc);
    void drawFooter(JKBMSData* data);
private:
    TFT_eSPI* tft;
    int animationFrame;
    uint16_t getPowerColor(float power);
    uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio);
};
#endif
EOF

# src/Battle_UI.cpp (简化版)
echo "生成 src/Battle_UI.cpp..."
cat > "$PROJECT_DIR/src/Battle_UI.cpp" << 'EOF'
#include "Battle_UI.h"
#include <Arduino.h>

BattleUI::BattleUI(TFT_eSPI* display) {
    tft = display;
    animationFrame = 0;
}

void BattleUI::begin() {
    tft->setTextDatum(MC_DATUM);
    tft->setTextWrap(false);
    tft->fillScreen(TFT_BLACK);
    drawBackground();
}

void BattleUI::drawBackground() {
    tft->fillScreen(TFT_BLACK);
    for (int i = 0; i < 40; i += 2) {
        uint16_t bgColor = tft->color565(16 + i/2, 16 + i/2, 32 + i);
        tft->drawFastHLine(0, i, 240, bgColor);
        tft->drawFastHLine(0, 319 - i, 240, bgColor);
    }
}

uint16_t BattleUI::interpolateColor(uint16_t color1, uint16_t color2, float ratio) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    return tft->color565(r1 + (r2 - r1) * ratio, g1 + (g2 - g1) * ratio, b1 + (b2 - b1) * ratio);
}

uint16_t BattleUI::getPowerColor(float power) {
    float ratio = abs(power) / 5000.0f;
    ratio = constrain(ratio, 0.0f, 1.0f);
    if (ratio < 0.33f) return interpolateColor(BATTLE_COLOR_LOW, BATTLE_COLOR_MED, ratio * 3.0f);
    else if (ratio < 0.66f) return interpolateColor(BATTLE_COLOR_MED, BATTLE_COLOR_HIGH, (ratio - 0.33f) * 3.0f);
    else return interpolateColor(BATTLE_COLOR_HIGH, BATTLE_COLOR_MAX, (ratio - 0.66f) * 3.0f);
}

void BattleUI::drawTitle() {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    tft->drawString("BATTLE MODE", 120, 12);
}

void BattleUI::drawBluetoothStatus(bool connected) {
    int16_t iconX = 210;
    int16_t iconY = 12;
    uint16_t color = connected ? 0x07E0 : 0xF800;
    uint16_t pulseColor = interpolateColor(color, 0xFFFF, (sin(animationFrame * 0.1) + 1) / 2);
    tft->fillCircle(iconX, iconY, 8, pulseColor);
    tft->drawCircle(iconX, iconY, 8, 0xFFFF);
    tft->setTextColor(TFT_BLACK, pulseColor);
    tft->setFreeFont(&NotoSansBold8);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(connected ? "B" : "X", iconX, iconY);
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(color, TFT_BLACK);
    tft->drawString(connected ? "CONNECTED" : "OFFLINE", iconX - 30, iconY + 12);
}

void BattleUI::drawPowerGauge(float power) {
    int16_t centerX = 120;
    int16_t centerY = 95;
    uint16_t powerColor = getPowerColor(power);
    
    tft->fillRoundRect(centerX - 85, centerY - 65, 170, 130, 8, 0x1082);
    
    uint16_t arcBgColor = tft->color565(40, 40, 60);
    tft->drawArc(centerX, centerY, 55, 65, 150, 240, arcBgColor);
    
    float powerRatio = constrain(abs(power) / 5000.0f, 0.0f, 1.0f);
    tft->drawArc(centerX, centerY, 55, 65, 150, 150 + 90 * powerRatio, powerColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold20);
    tft->drawString(String((int)abs(power)).c_str(), centerX, centerY - 10);
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("W", centerX + 40, centerY + 5);
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->drawString("REAL POWER", centerX, centerY - 40);
    
    String status = power > 0 ? "DISCHARGING" : (power < 0 ? "CHARGING" : "STANDBY");
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString(status.c_str(), centerX, centerY + 35);
}

void BattleUI::drawCapacity(float remaining, float total) {
    int16_t cardX = 15;
    int16_t cardY = 145;
    int16_t cardW = 210;
    int16_t cardH = 60;
    
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x2945);
    
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString("CAPACITY", cardX + 10, cardY + 12);
    
    float ratio = total > 0 ? (remaining / total) : 0;
    tft->fillRoundRect(cardX + 10, cardY + 28, cardW - 20, 20, 5, tft->color565(30, 30, 50));
    tft->fillRoundRect(cardX + 10, cardY + 28, (cardW - 20) * ratio, 20, 5, interpolateColor(0x07E0, 0xF800, 1.0f - ratio));
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString(String(remaining, 1) + " / " + String(total, 1) + " Ah", cardX + cardW / 2, cardY + 38);
    
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->drawString(String((int)(ratio * 100)) + "%", cardX + cardW - 15, cardY + 12);
}

void BattleUI::drawTemperatures(int16_t temp1, int16_t temp2) {
    int16_t cardX = 15;
    int16_t cardY = 215;
    int16_t cardW = 100;
    int16_t cardH = 55;
    
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, 0x1082);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("TEMPERATURE", cardX + cardW / 2, cardY + 10);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    tft->drawString(String(temp1) + "C", cardX + 35, cardY + 32);
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString(String(temp2) + "C", cardX + 80, cardY + 32);
    
    tft->fillRoundRect(cardX + 105, cardY, cardW, cardH, 8, 0x1082);
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("VOLTAGE", cardX + 155, cardY + 10);
}

void BattleUI::drawSOC(uint8_t soc) {
    int16_t cardX = 15;
    int16_t cardY = 280;
    int16_t cardW = 210;
    int16_t cardH = 35;
    
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, interpolateColor(0x07E0, 0xF800, 1.0f - soc / 100.0f));
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_BLACK, interpolateColor(0x07E0, 0xF800, 1.0f - soc / 100.0f));
    tft->setFreeFont(&NotoSansBold12);
    tft->drawString("SOC: " + String(soc) + "%", cardX + cardW / 2, cardY + cardH / 2);
}

void BattleUI::drawFooter(JKBMSData* data) {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    if (data != nullptr) {
        tft->drawString("Cycles: " + String(data->cycleCount) + " | Cells: " + String(data->cellCount) + "S", 120, 310);
    } else {
        tft->drawString("Waiting for data...", 120, 310);
    }
}

void BattleUI::update(JKBMSData* bmsData, bool bluetoothConnected) {
    animationFrame++;
    drawBackground();
    drawTitle();
    drawBluetoothStatus(bluetoothConnected);
    
    if (bmsData != nullptr) {
        drawPowerGauge(bmsData->power);
        drawCapacity(bmsData->capacityRemaining, bmsData->capacityTotal);
        drawTemperatures(bmsData->temperature1, bmsData->temperature2);
        drawSOC((uint8_t)bmsData->soc);
    } else {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(0xFFFF, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold16);
        tft->drawString("WAITING...", 120, 120);
        tft->setFreeFont(&NotoSansBold10);
        tft->drawString("Scanning JK_BD4A24S10P", 120, 150);
    }
    drawFooter(bmsData);
}
EOF

echo ""
echo "=========================================="
echo "✓ 所有核心文件已生成!"
echo "=========================================="
echo ""
echo "📦 项目已创建: $PROJECT_DIR"
echo ""
echo "文件列表:"
ls -la "$PROJECT_DIR/src/"
ls -la "$PROJECT_DIR/"
echo ""
echo "下一步:"
echo "1. cd $PROJECT_DIR"
echo "2. pio run  (编译)"
echo "3. pio run --target upload  (上传)"
echo ""
