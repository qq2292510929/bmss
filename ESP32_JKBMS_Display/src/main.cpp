#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <esp_bt_main.h>
#include <esp_bluedroid.h>
#include "JKBMS_Protocol.h"
#include "JKBMS_Bluetooth.h"
#include "Battle_UI.h"

#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_MISO  19

#define BT_DEVICE_NAME "JK_BD4A24S10P"
#define BT_MAC_ADDRESS "98:da:20:07:b9:00"
#define JK_PROTOCOL_VERSION "JK02_32S"

TFT_eSPI tft = TFT_eSPI(240, 320);
JKBMSBluetooth bmsBT;
BattleUI battleUI(&tft);
JKBMSData bmsData;
bool bluetoothConnected = false;
unsigned long lastBMSUpdate = 0;
unsigned long lastScreenUpdate = 0;
bool displayInitialized = false;

void initDisplay() {
    Serial.println("Initializing ST7789 Display...");
    
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFreeFont(&NotoSansBold12);
    tft.setTextDatum(MC_DATUM);
    
    tft.drawString("初始化屏幕...", 160, 160);
    
    delay(500);
    
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

void connectToBMS() {
    Serial.println("Connecting to JK BMS...");
    Serial.print("Device Name: ");
    Serial.println(BT_DEVICE_NAME);
    Serial.print("MAC Address: ");
    Serial.println(BT_MAC_ADDRESS);
    Serial.print("Protocol: ");
    Serial.println(JK_PROTOCOL_VERSION);
    
    bluetoothConnected = bmsBT.connectToBMS(BT_DEVICE_NAME, BT_MAC_ADDRESS);
    
    if (bluetoothConnected) {
        Serial.println("Successfully connected to JK BMS!");
    } else {
        Serial.println("Failed to connect to JK BMS!");
    }
}

void updateBMSData() {
    if (bmsBT.hasNewData()) {
        bmsData = bmsBT.getBMSData();
        lastBMSUpdate = millis();
        
        Serial.println("=== BMS Data Update ===");
        Serial.print("Total Voltage: ");
        Serial.print(bmsData.totalVoltage);
        Serial.println(" V");
        Serial.print("Current: ");
        Serial.print(bmsData.current);
        Serial.println(" A");
        Serial.print("Power: ");
        Serial.print(bmsData.power);
        Serial.println(" W");
        Serial.print("Capacity: ");
        Serial.print(bmsData.capacityRemaining);
        Serial.print(" / ");
        Serial.print(bmsData.capacityTotal);
        Serial.println(" Ah");
        Serial.print("SOC: ");
        Serial.print(bmsData.soc);
        Serial.println(" %");
        Serial.print("Temperature 1: ");
        Serial.print(bmsData.temperature1);
        Serial.println(" °C");
        Serial.print("Temperature 2: ");
        Serial.print(bmsData.temperature2);
        Serial.println(" °C");
        Serial.print("Cycle Count: ");
        Serial.println(bmsData.cycleCount);
        Serial.print("Cell Count: ");
        Serial.println(bmsData.cellCount);
        Serial.print("Cell Voltages: ");
        for (int i = 0; i < bmsData.cellCount && i < 4; i++) {
            Serial.print(bmsData.cellVoltages[i], 3);
            Serial.print("V ");
        }
        Serial.println();
        Serial.println("========================");
    }
}

void updateDisplay() {
    if (!displayInitialized) return;
    
    JKBMSData* dataPtr = nullptr;
    if (bluetoothConnected) {
        dataPtr = &bmsData;
    }
    
    battleUI.update(dataPtr, bluetoothConnected);
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
    
    connectToBMS();
    
    Serial.println("Setup complete! Starting main loop...");
}

void loop() {
    unsigned long currentTime = millis();
    
    updateBMSData();
    
    if (currentTime - lastScreenUpdate >= 100) {
        updateDisplay();
        lastScreenUpdate = currentTime;
    }
    
    if (!bluetoothConnected && currentTime > 30000) {
        Serial.println("Connection lost, attempting reconnection...");
        bluetoothConnected = bmsBT.connectToBMS(BT_DEVICE_NAME, BT_MAC_ADDRESS);
        delay(1000);
    }
    
    delay(10);
}
