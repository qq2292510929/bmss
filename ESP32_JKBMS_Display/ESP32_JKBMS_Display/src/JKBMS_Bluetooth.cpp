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
