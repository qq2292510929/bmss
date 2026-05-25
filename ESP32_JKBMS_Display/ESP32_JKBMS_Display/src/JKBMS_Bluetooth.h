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
