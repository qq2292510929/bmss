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
