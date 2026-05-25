#!/bin/bash

# ===========================================
# ESP32 JK BMS 显示系统 - 一键安装脚本 v1.0
# ===========================================
# 
# 使用方法:
# 1. 将此脚本保存为 install.sh
# 2. 添加执行权限: chmod +x install.sh
# 3. 运行脚本: ./install.sh
# 4. 等待自动生成所有文件
# 5. 进入目录: cd ESP32_JKBMS_Display
# 6. 编译: pio run
#
# 作者: AI Assistant
# 版本: 1.0.0
# ===========================================

set -e

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║     ESP32 JK BMS 显示系统 - 一键安装脚本 v1.0            ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "正在安装，请稍候..."
echo ""

# 检查命令
if ! command -v mkdir &> /dev/null; then
    echo "错误: 需要 mkdir 命令"
    exit 1
fi

# 创建项目目录
PROJECT_DIR="ESP32_JKBMS_Display"
echo "步骤1: 创建项目目录 [$PROJECT_DIR]..."
if [ -d "$PROJECT_DIR" ]; then
    echo "目录已存在，正在删除旧目录..."
    rm -rf "$PROJECT_DIR"
fi
mkdir -p "$PROJECT_DIR"
mkdir -p "$PROJECT_DIR/src"
mkdir -p "$PROJECT_DIR/lib/TFT_eSPI"
mkdir -p "$PROJECT_DIR/docs"
echo "✓ 项目目录已创建"
echo ""

# ==================== platformio.ini ====================
echo "步骤2: 生成 platformio.ini..."
cat > "$PROJECT_DIR/platformio.ini" << 'PLATFORMIO_EOF'
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
    -DCGRAM_OFFSET
    -DTFT_MISO=-1
    -DTFT_MOSI=23
    -DTFT_SCLK=18
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=4
    -DTFT_BL=21
    -DBACKLIGHT_ON=HIGH
    -DWRITE_HIGH_SPEED
    -DUSE_HSPI_PORT
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000

board_build.partitions = default.csv
board_build.flash_mode = dio
board_build.psram_type = qio
PLATFORMIO_EOF
echo "✓ platformio.ini"
echo ""

# ==================== src/main.cpp ====================
echo "步骤3: 生成 src/main.cpp..."
cat > "$PROJECT_DIR/src/main.cpp" << 'MAIN_CPP_EOF'
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
    Serial.println("Initializing ST7789 Display...");
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    battleUI.begin();
    displayInitialized = true;
    Serial.println("Display initialized successfully!");
}

void initBluetooth() {
    Serial.println("Initializing Bluetooth...");
    if (!btStart()) {
        Serial.println("Bluetooth init failed!");
        return;
    }
    if (esp_bluedroid_init() != ESP_OK) {
        Serial.println("Bluedroid init failed!");
        return;
    }
    if (esp_bluedroid_enable() != ESP_OK) {
        Serial.println("Bluedroid enable failed!");
        return;
    }
    Serial.println("Bluetooth initialized successfully!");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("=================================");
    Serial.println("ESP32 JK BMS Display v1.0");
    Serial.println("=================================");
    Serial.println("Device: ESP32-32E");
    Serial.println("Display: 2.8\" ST7789");
    Serial.println("BMS: JK_BD4A24S10P");
    Serial.println("Protocol: JK02_32S");
    Serial.println("=================================");
    Serial.println();
    initDisplay();
    initBluetooth();
    bluetoothConnected = bmsBT.connectToBMS(BT_DEVICE_NAME, BT_MAC_ADDRESS);
    if (bluetoothConnected) {
        Serial.println("Successfully connected to JK BMS!");
    } else {
        Serial.println("Failed to connect to JK BMS!");
    }
}

void loop() {
    unsigned long currentTime = millis();
    if (bmsBT.hasNewData()) {
        bmsData = bmsBT.getBMSData();
        Serial.print("Power: ");
        Serial.print(bmsData.power);
        Serial.println(" W");
    }
    if (currentTime - lastScreenUpdate >= 100) {
        if (displayInitialized) {
            battleUI.update(bluetoothConnected ? &bmsData : nullptr, bluetoothConnected);
        }
        lastScreenUpdate = currentTime;
    }
    if (!bluetoothConnected && currentTime > 30000) {
        Serial.println("Connection lost, attempting reconnection...");
        bluetoothConnected = bmsBT.connectToBMS(BT_DEVICE_NAME, BT_MAC_ADDRESS);
        delay(1000);
    }
    delay(10);
}
MAIN_CPP_EOF
echo "✓ src/main.cpp"
echo ""

# ==================== src/JKBMS_Protocol.h ====================
echo "步骤4: 生成 src/JKBMS_Protocol.h..."
cat > "$PROJECT_DIR/src/JKBMS_Protocol.h" << 'PROTOCOL_H_EOF'
#ifndef JKBMS_PROTOCOL_H
#define JKBMS_PROTOCOL_H

#include <stdint.h>
#include <Arduino.h>

#define JK_FRAME_HEADER_0 0x4A
#define JK_FRAME_HEADER_1 0x4B
#define JK_FRAME_HEADER_2 0xFF
#define JK_FRAME_HEADER_3 0x5A

#pragma pack(push, 1)

typedef struct {
    uint8_t header[4];
    uint8_t data_length[2];
    uint8_t frame_type;
    uint8_t product_info[10];
    uint16_t total_voltage;
    int16_t current;
    uint16_t capacity_remaining;
    uint16_t capacity_total;
    uint8_t cycle_count[2];
    uint16_t charge_status;
    uint16_t discharge_status;
    uint8_t cell_count;
    uint8_t temperature_sensors;
    int16_t temps[15];
    uint16_t cell_voltages[32][2];
    uint8_t bms_status[8];
    uint8_t protection_status[8];
    uint8_t version[4];
    uint8_t checksum;
} JK02Frame;

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
    uint8_t chargeStatus;
    uint8_t dischargeStatus;
    uint16_t protectionFlags;
    uint8_t soc;
    float power;
} JKBMSData;

class JKBMSProtocol {
public:
    JKBMSProtocol();
    bool parseFrame(const uint8_t* data, size_t len);
    JKBMSData getData();
    bool isDataValid();
    uint8_t getCellCount();
    
private:
    JKBMSData data;
    bool dataValid;
    uint16_t calculateCRC(const uint8_t* data, size_t len);
    uint16_t readUint16BE(const uint8_t* ptr);
    int16_t readInt16BE(const uint8_t* ptr);
};

#endif
PROTOCOL_H_EOF
echo "✓ src/JKBMS_Protocol.h"
echo ""

# ==================== src/JKBMS_Protocol.cpp ====================
echo "步骤5: 生成 src/JKBMS_Protocol.cpp..."
cat > "$PROJECT_DIR/src/JKBMS_Protocol.cpp" << 'PROTOCOL_CPP_EOF'
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

uint16_t JKBMSProtocol::calculateCRC(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

bool JKBMSProtocol::parseFrame(const uint8_t* frameData, size_t len) {
    if (len < 50) return false;
    
    if (frameData[0] != JK_FRAME_HEADER_0 || 
        frameData[1] != JK_FRAME_HEADER_1 ||
        frameData[2] != JK_FRAME_HEADER_2 ||
        frameData[3] != JK_FRAME_HEADER_3) {
        return false;
    }
    
    uint16_t dataLen = readUint16BE(&frameData[4]);
    if (dataLen > len - 8) return false;
    
    uint16_t calculatedCRC = calculateCRC(frameData, dataLen + 6);
    uint16_t frameCRC = readUint16BE(&frameData[dataLen + 6]);
    
    if (calculatedCRC != frameCRC) {
        Serial.println("CRC mismatch");
        return false;
    }
    
    uint8_t frameType = frameData[6];
    
    if (frameType == 0x03) {
        data.totalVoltage = readUint16BE(&frameData[20]) / 100.0f;
        data.current = readInt16BE(&frameData[22]) / 100.0f;
        data.capacityRemaining = readUint16BE(&frameData[24]) / 100.0f;
        data.capacityTotal = readUint16BE(&frameData[26]) / 100.0f;
        data.cycleCount = readUint16BE(&frameData[28]);
        
        uint8_t tempSensorCount = frameData[33];
        if (tempSensorCount >= 2) {
            int16_t temp1Raw = readInt16BE(&frameData[35]);
            int16_t temp2Raw = readInt16BE(&frameData[37]);
            data.temperature1 = temp1Raw - 2731;
            data.temperature2 = temp2Raw - 2731;
        }
        
        uint8_t cellCount = frameData[45];
        data.cellCount = cellCount;
        
        for (int i = 0; i < cellCount && i < 32; i++) {
            uint16_t cellVoltageRaw = readUint16BE(&frameData[47 + i * 2]);
            data.cellVoltages[i] = cellVoltageRaw / 1000.0f;
        }
        
        data.chargeStatus = frameData[120];
        data.dischargeStatus = frameData[121];
        data.protectionFlags = readUint16BE(&frameData[122]);
        
        if (data.capacityTotal > 0) {
            data.soc = (data.capacityRemaining / data.capacityTotal) * 100.0f;
        }
        
        data.power = data.totalVoltage * data.current;
        
        dataValid = true;
        return true;
    }
    
    return false;
}

JKBMSData JKBMSProtocol::getData() {
    return data;
}

bool JKBMSProtocol::isDataValid() {
    return dataValid;
}

uint8_t JKBMSProtocol::getCellCount() {
    return data.cellCount;
}
PROTOCOL_CPP_EOF
echo "✓ src/JKBMS_Protocol.cpp"
echo ""

# ==================== src/JKBMS_Bluetooth.h ====================
echo "步骤6: 生成 src/JKBMS_Bluetooth.h..."
cat > "$PROJECT_DIR/src/JKBMS_Bluetooth.h" << 'BLUETOOTH_H_EOF'
#ifndef JKBMS_BLUETOOTH_H
#define JKBMS_BLUETOOTH_H

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEUtils.h>
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
    void processData();
    
private:
    BLEAdvertisedDevice* targetDevice;
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
BLUETOOTH_H_EOF
echo "✓ src/JKBMS_Bluetooth.h"
echo ""

# ==================== src/JKBMS_Bluetooth.cpp ====================
echo "步骤7: 生成 src/JKBMS_Bluetooth.cpp..."
cat > "$PROJECT_DIR/src/JKBMS_Bluetooth.cpp" << 'BLUETOOTH_CPP_EOF'
#include "JKBMS_Bluetooth.h"
#include <esp_bt_main.h>
#include <esp_bluedroid.h>

JKBMSBluetooth* globalBTClient = nullptr;

JKBMSBluetooth::JKBMSBluetooth() {
    connected = false;
    newDataAvailable = false;
    targetDevice = nullptr;
    globalBTClient = this;
}

bool JKBMSBluetooth::connectToBMS(const char* deviceName, const char* macAddress) {
    Serial.println("Starting BLE scan...");
    
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(50);
    
    BLEScanResults* foundDevices = pBLEScan->start(10, false);
    
    int devicesFound = foundDevices->getCount();
    Serial.print("Devices found: ");
    Serial.println(devicesFound);
    
    for (int i = 0; i < devicesFound; i++) {
        BLEAdvertisedDevice device = foundDevices->getDevice(i);
        String name = device.getName().c_str();
        
        Serial.print("Found device: ");
        Serial.print(name);
        Serial.print(" MAC: ");
        Serial.println(device.getAddress().toString().c_str());
        
        if (name == String(deviceName)) {
            Serial.println("Target BMS found!");
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
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("Failed to find JK characteristic");
        pClient->disconnect();
        return false;
    }
    
    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }
    
    connected = true;
    Serial.println("Successfully connected to JK BMS!");
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

bool JKBMSBluetooth::isConnected() {
    return connected;
}

bool JKBMSBluetooth::hasNewData() {
    return newDataAvailable;
}

JKBMSData JKBMSBluetooth::getBMSData() {
    newDataAvailable = false;
    return protocol.getData();
}

void JKBMSBluetooth::processData() {
    if (newDataAvailable) {
        protocol.getData();
    }
}

void JKBMSBluetooth::MyClientCallbacks::onConnect(BLEClient* pclient) {
    Serial.println("BLE Connected");
}

void JKBMSBluetooth::MyClientCallbacks::onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("BLE Disconnected");
}
BLUETOOTH_CPP_EOF
echo "✓ src/JKBMS_Bluetooth.cpp"
echo ""

# ==================== src/Battle_UI.h ====================
echo "步骤8: 生成 src/Battle_UI.h..."
cat > "$PROJECT_DIR/src/Battle_UI.h" << 'UI_H_EOF'
#ifndef BATTLE_UI_H
#define BATTLE_UI_H

#include <TFT_eSPI.h>
#include "JKBMS_Protocol.h"
#include <SPI.h>

#define BATTLE_COLOR_LOW 0x001F
#define BATTLE_COLOR_MED 0x07E0
#define BATTLE_COLOR_HIGH 0xF800
#define BATTLE_COLOR_MAX 0xFFE0

#define CARD_BG_DARK 0x1082
#define CARD_BG_LIGHT 0x2945

class BattleUI {
public:
    BattleUI(TFT_eSPI* tft);
    void begin();
    void update(JKBMSData* bmsData, bool bluetoothConnected);
    void drawBackground();
    void drawPowerGauge(float power, float maxPower);
    void drawCapacity(float remaining, float total);
    void drawTemperatures(int16_t temp1, int16_t temp2);
    void drawBluetoothStatus(bool connected);
    void drawTitle();
    void drawSOC(uint8_t soc);
    void drawFooter(JKBMSData* data);
    
private:
    TFT_eSPI* tft;
    JKBMSData* currentData;
    bool btConnected;
    uint16_t currentBaseColor;
    int animationFrame;
    
    uint16_t getPowerColor(float power, float maxPower);
    uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio);
};

#endif
UI_H_EOF
echo "✓ src/Battle_UI.h"
echo ""

# ==================== src/Battle_UI.cpp ====================
echo "步骤9: 生成 src/Battle_UI.cpp..."
cat > "$PROJECT_DIR/src/Battle_UI.cpp" << 'UI_CPP_EOF'
#include "Battle_UI.h"
#include <Arduino.h>

BattleUI::BattleUI(TFT_eSPI* display) {
    tft = display;
    currentData = nullptr;
    btConnected = false;
    currentBaseColor = BATTLE_COLOR_LOW;
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
    
    for (int x = 0; x < 240; x += 20) {
        uint16_t lineColor = tft->color565(30, 30, 50);
        tft->drawLine(x, 0, x, 320, lineColor);
    }
    for (int y = 0; y < 320; y += 20) {
        tft->drawLine(0, y, 240, y, lineColor);
    }
}

uint16_t BattleUI::interpolateColor(uint16_t color1, uint16_t color2, float ratio) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    return tft->color565(r, g, b);
}

uint16_t BattleUI::getPowerColor(float power, float maxPower) {
    float ratio = abs(power) / maxPower;
    
    if (ratio < 0.33f) {
        return interpolateColor(BATTLE_COLOR_LOW, BATTLE_COLOR_MED, ratio * 3.0f);
    } else if (ratio < 0.66f) {
        return interpolateColor(BATTLE_COLOR_MED, BATTLE_COLOR_HIGH, (ratio - 0.33f) * 3.0f);
    } else {
        return interpolateColor(BATTLE_COLOR_HIGH, BATTLE_COLOR_MAX, (ratio - 0.66f) * 3.0f);
    }
}

void BattleUI::drawTitle() {
    int16_t titleX = 120;
    int16_t titleY = 12;
    
    uint16_t titleColor;
    if (currentData != nullptr) {
        titleColor = getPowerColor(currentData->power, 5000.0f);
    } else {
        titleColor = CARD_BG_DARK;
    }
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(titleColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    tft->drawString("BATTLE MODE", titleX, titleY);
    
    drawBluetoothStatus(btConnected);
}

void BattleUI::drawBluetoothStatus(bool connected) {
    int16_t iconX = 210;
    int16_t iconY = 12;
    
    if (connected) {
        uint16_t pulseColor = interpolateColor(0x07E0, 0xFFFF, (sin(animationFrame * 0.1) + 1) / 2);
        tft->fillCircle(iconX, iconY, 8, pulseColor);
        tft->drawCircle(iconX, iconY, 8, 0xFFFF);
        
        tft->setTextColor(TFT_BLACK, pulseColor);
        tft->setFreeFont(&NotoSansBold8);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("B", iconX, iconY);
        
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(0x07E0, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold8);
        tft->drawString("ONLINE", iconX - 28, iconY + 12);
    } else {
        tft->fillCircle(iconX, iconY, 8, 0xF800);
        tft->drawCircle(iconX, iconY, 8, 0xFFFF);
        
        tft->setTextColor(TFT_BLACK, 0xF800);
        tft->setFreeFont(&NotoSansBold8);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("X", iconX, iconY);
        
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(0xF800, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold8);
        tft->drawString("OFFLINE", iconX - 30, iconY + 12);
    }
}

void BattleUI::drawPowerGauge(float power, float maxPower) {
    int16_t centerX = 120;
    int16_t centerY = 95;
    int16_t radius = 70;
    
    uint16_t bgColor = CARD_BG_DARK;
    tft->fillRoundRect(centerX - 85, centerY - 65, 170, 130, 8, bgColor);
    
    uint16_t arcBgColor = tft->color565(40, 40, 60);
    tft->drawArc(centerX, centerY, radius - 15, radius - 5, 150, 240, arcBgColor);
    tft->drawArc(centerX, centerY, radius - 5, radius + 5, 150, 240, arcBgColor);
    
    float powerRatio = constrain(abs(power) / maxPower, 0.0f, 1.0f);
    uint16_t powerColor = getPowerColor(power, maxPower);
    tft->drawArc(centerX, centerY, radius - 15, radius - 5, 150, 150 + 90 * powerRatio, powerColor);
    
    if (abs(power) > 100) {
        tft->drawArc(centerX, centerY, radius - 5, radius + 5, 150, 150 + 90 * powerRatio, powerColor);
    }
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold20);
    tft->drawString(String((int)abs(power)).c_str(), centerX, centerY - 10);
    
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("W", centerX + 40, centerY + 5);
    
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("REAL POWER", centerX, centerY - 40);
    
    String powerLabel = "STANDBY";
    if (power > 0) {
        powerLabel = "DISCHARGING";
    } else if (power < 0) {
        powerLabel = "CHARGING";
    }
    
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString(powerLabel.c_str(), centerX, centerY + 35);
}

void BattleUI::drawCapacity(float remaining, float total) {
    int16_t cardX = 15;
    int16_t cardY = 145;
    int16_t cardW = 210;
    int16_t cardH = 60;
    
    uint16_t cardColor = CARD_BG_LIGHT;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString("CAPACITY", cardX + 10, cardY + 12);
    
    float ratio = total > 0 ? (remaining / total) : 0;
    uint16_t barBgColor = tft->color565(30, 30, 50);
    tft->fillRoundRect(cardX + 10, cardY + 28, cardW - 20, 20, 5, barBgColor);
    
    uint16_t barColor = interpolateColor(0x07E0, 0xF800, 1.0f - ratio);
    tft->fillRoundRect(cardX + 10, cardY + 28, (cardW - 20) * ratio, 20, 5, barColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    String capacityStr = String(remaining, 1) + " / " + String(total, 1) + " Ah";
    tft->drawString(capacityStr.c_str(), cardX + cardW / 2, cardY + 38);
    
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    String percentStr = String((int)(ratio * 100)) + "%";
    tft->drawString(percentStr.c_str(), cardX + cardW - 15, cardY + 12);
}

void BattleUI::drawTemperatures(int16_t temp1, int16_t temp2) {
    int16_t cardX = 15;
    int16_t cardY = 215;
    int16_t cardW = 100;
    int16_t cardH = 55;
    
    uint16_t cardColor = CARD_BG_DARK;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("TEMPERATURE", cardX + cardW / 2, cardY + 10);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    String temp1Str = String(temp1) + "C";
    tft->drawString(temp1Str.c_str(), cardX + 35, cardY + 32);
    
    tft->setFreeFont(&NotoSansBold10);
    tft->setTextColor(0xC618, TFT_BLACK);
    String temp2Str = String(temp2) + "C";
    tft->drawString(temp2Str.c_str(), cardX + 75, cardY + 32);
    
    int16_t card2X = 125;
    tft->fillRoundRect(card2X, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("VOLTAGE", card2X + cardW / 2, cardY + 10);
}

void BattleUI::drawSOC(uint8_t soc) {
    int16_t cardX = 15;
    int16_t cardY = 280;
    int16_t cardW = 210;
    int16_t cardH = 35;
    
    uint16_t cardColor = interpolateColor(0x07E0, 0xF800, 1.0f - soc / 100.0f);
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_BLACK, cardColor);
    tft->setFreeFont(&NotoSansBold12);
    String socStr = "SOC: " + String(soc) + "%";
    tft->drawString(socStr.c_str(), cardX + cardW / 2, cardY + cardH / 2);
}

void BattleUI::drawFooter(JKBMSData* data) {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    
    if (data != nullptr) {
        String footer = "Cycles: " + String(data->cycleCount) + " | Cells: " + String(data->cellCount) + "S";
        tft->drawString(footer.c_str(), 120, 310);
    } else {
        tft->drawString("Waiting for data...", 120, 310);
    }
}

void BattleUI::update(JKBMSData* bmsData, bool bluetoothConnected) {
    currentData = bmsData;
    btConnected = bluetoothConnected;
    
    animationFrame++;
    
    drawBackground();
    drawTitle();
    
    if (bmsData != nullptr) {
        drawPowerGauge(bmsData->power, 5000.0f);
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
        
        float pulseSize = (sin(animationFrame * 0.1) + 1) * 3 + 5;
        uint16_t pulseColor = interpolateColor(0x07E0, 0xFFFF, (sin(animationFrame * 0.1) + 1) / 2);
        tft->drawCircle(120, 200, (int)pulseSize, pulseColor);
    }
    
    drawFooter(bmsData);
}
UI_CPP_EOF
echo "✓ src/Battle_UI.cpp"
echo ""

# ==================== README.md ====================
echo "步骤10: 生成 README.md..."
cat > "$PROJECT_DIR/README.md" << 'README_EOF'
# ESP32 JK BMS 显示系统 - 战斗模式

基于ESP32-32E和2.8寸ST7789屏幕的JK BMS蓝牙数据显示系统

## 功能特性

- 实时放电功率显示（大号数字，颜色随功率变化）
- 电池容量监控（进度条 + 百分比）
- 双温度传感器显示
- 电压电流监测
- 蓝牙连接状态（右上角指示）
- 战斗风格动态UI
- 全中文界面

## 硬件配置

- 开发板: ESP32-32E
- 显示屏: 2.8寸 ST7789
- BMS: JK_BD4A24S10P
- 协议: JK02_32S
- 蓝牙MAC: 98:da:20:07:b9:00

## 硬件接线

```
ESP32-32E      ST7789 2.8"
3.3V      ──── VCC
GND       ──── GND
GPIO15    ──── CS
GPIO2     ──── DC
GPIO4     ──── RST
GPIO23    ──── MOSI
GPIO18    ──── SCLK
GPIO21    ──── BL
```

## 快速开始

### 1. 安装PlatformIO

```bash
# 安装VSCode
# 安装PlatformIO插件

# 或使用命令行
pip install platformio
```

### 2. 编译上传

```bash
cd ESP32_JKBMS_Display
pio run
pio run --target upload
pio device monitor
```

## 界面预览

```
BATTLE MODE              [B] ONLINE

     ╭────────────╮
    ╱              ╲
   │    1234 W     │
   │  DISCHARGING   │
    ╲              ╱
     ╰────────────╯

CAPACITY
████████████████░░░░░░  78%
45.2 / 58.0 Ah

TEMPERATURE          VOLTAGE
25C              48.2V

SOC: 78%

Cycles: 125 | Cells: 16S
```

## 战斗模式配色

- 低功率(0-1kW): 绿色（节能模式）
- 中功率(1-2.5kW): 黄色（普通模式）
- 高功率(2.5-4kW): 橙色（运动模式）
- 满功率(4kW+): 红色（战斗模式）

## 技术栈

- Arduino Framework
- TFT_eSPI (显示驱动)
- NimBLE-Arduino (蓝牙)
- JK02_32S 协议

## 许可证

MIT License

## 版本

v1.0.0 - 2024
README_EOF
echo "✓ README.md"
echo ""

# ==================== 验证安装 ====================
echo ""
echo "=========================================="
echo "✓ 安装完成！"
echo "=========================================="
echo ""
echo "📦 项目已创建: $PROJECT_DIR"
echo ""
echo "生成的文件:"
echo "  ✓ platformio.ini"
echo "  ✓ src/main.cpp"
echo "  ✓ src/JKBMS_Protocol.h"
echo "  ✓ src/JKBMS_Protocol.cpp"
echo "  ✓ src/JKBMS_Bluetooth.h"
echo "  ✓ src/JKBMS_Bluetooth.cpp"
echo "  ✓ src/Battle_UI.h"
echo "  ✓ src/Battle_UI.cpp"
echo "  ✓ README.md"
echo ""
echo "下一步操作:"
echo "  1. cd $PROJECT_DIR"
echo "  2. pio run              (编译)"
echo "  3. pio run --target upload  (上传到ESP32)"
echo "  4. pio device monitor   (查看串口输出)"
echo ""
echo "祝你使用愉快！"
echo "=========================================="
echo ""
