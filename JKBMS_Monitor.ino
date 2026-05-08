// 在最前面定义 ESP_PLATFORM，让 NimBLE 库知道我们在 ESP32 上
#define ESP_PLATFORM

#include <Arduino.h>
#include <NimBLEDevice.h>

// 暂时注释掉屏幕相关代码，避免编译错误
// #include <TFT_eSPI.h>

// JKBMS BLE 配置
#define BMS_MAC_ADDRESS "98:DA:20:07:B9:00"
#define BMS_DEVICE_NAME "JK_BD4A24S10P"
#define JK_BMS_SERVICE_UUID "ffe0"
#define JK_BMS_CHARACTERISTIC_UUID "ffe1"
#define COMMAND_DEVICE_INFO 0x97
#define COMMAND_CELL_INFO 0x96
#define MIN_RESPONSE_SIZE 300
#define MAX_RESPONSE_SIZE 320

// 全局变量
NimBLEClient* pClient;
NimBLERemoteService* pRemoteService;
NimBLERemoteCharacteristic* pRemoteCharacteristic;
bool connected = false;

// 回调函数类
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    Serial.println("Connected to BMS!");
    connected = true;
  }

  void onDisconnect(NimBLEClient* pClient) {
    Serial.println("Disconnected from BMS!");
    connected = false;
  }
};

// 通知回调
void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  Serial.print("Received data, length: ");
  Serial.println(length);
  
  // 打印接收到的数据（十六进制）
  Serial.print("Data: ");
  for (int i = 0; i < length; i++) {
    Serial.printf("%02X ", pData[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting JKBMS BLE Monitor...");
  
  // 初始化 NimBLE
  NimBLEDevice::init("");
  
  // 创建客户端
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCallbacks());
  
  Serial.println("Scanning for JKBMS...");
}

void loop() {
  if (!connected) {
    // 扫描设备
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(5);
    
    bool found = false;
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice device = results.getDevice(i);
      
      Serial.print("Found: ");
      Serial.print(device.getName().c_str());
      Serial.print(" [");
      Serial.print(device.getAddress().toString().c_str());
      Serial.println("]");
      
      if (device.getName() == BMS_DEVICE_NAME) {
        Serial.println("Found JKBMS! Connecting...");
        found = true;
        
        // 连接设备
        if (pClient->connect(&device)) {
          Serial.println("Connected!");
          
          // 获取服务
          pRemoteService = pClient->getService(JK_BMS_SERVICE_UUID);
          if (pRemoteService) {
            Serial.println("Found service!");
            
            // 获取特征值
            pRemoteCharacteristic = pRemoteService->getCharacteristic(JK_BMS_CHARACTERISTIC_UUID);
            if (pRemoteCharacteristic) {
              Serial.println("Found characteristic!");
              
              // 注册通知回调
              if (pRemoteCharacteristic->canNotify()) {
                pRemoteCharacteristic->subscribe(true, notifyCallback);
                Serial.println("Subscribed to notifications!");
              }
            }
          }
        } else {
          Serial.println("Connection failed!");
        }
        break;
      }
    }
    
    if (!found) {
      Serial.println("JKBMS not found, retrying...");
    }
    
    pScan->clearResults();
    delay(2000);
  } else {
    // 已连接，可以发送命令
    // 示例：发送获取设备信息的命令
    // uint8_t command[] = {0xAA, 0x55, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    // pRemoteCharacteristic->writeValue(command, sizeof(command));
    
    delay(5000);
  }
}
