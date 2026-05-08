// 在最前面定义 ESP_PLATFORM，让 NimBLE 库知道我们在 ESP32 上
#define ESP_PLATFORM

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

// JKBMS BLE 配置
// 请将下面的地址替换为你的 JKBMS 的 MAC 地址
#define BMS_MAC_ADDRESS "AA:BB:CC:DD:EE:FF"

// 全局变量
NimBLEScan* pBLEScan;
bool deviceFound = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting NimBLE Client...");
  
  // 初始化 NimBLE
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setActiveScan(true);
}

void loop() {
  if (!deviceFound) {
    Serial.println("Scanning for BLE devices...");
    
    NimBLEScanResults results = pBLEScan->start(5);
    
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice device = results.getDevice(i);
      Serial.print("Found device: ");
      Serial.print(device.getName().c_str());
      Serial.print(" [");
      Serial.print(device.getAddress().toString().c_str());
      Serial.println("]");
    }
    
    delay(5000);
  }
}
