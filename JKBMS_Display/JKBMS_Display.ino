#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

// ============================================================
// JKBMS BLE 配置
// ============================================================
#define BMS_MAC_ADDRESS     "98:DA:20:07:B9:00"
#define BMS_DEVICE_NAME     "JK_BD4A24S10P"

#define JK_BMS_SERVICE_UUID         "ffe0"
#define JK_BMS_CHARACTERISTIC_UUID  "ffe1"

#define COMMAND_DEVICE_INFO 0x97
#define COMMAND_CELL_INFO   0x96

#define MIN_RESPONSE_SIZE   300
#define MAX_RESPONSE_SIZE   320

// ============================================================
// 调试开关
// ============================================================
#define DEBUG_ENABLED true
#if DEBUG_ENABLED
  #define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// ============================================================
// TFT 显示对象
// ============================================================
TFT_eSPI tft = TFT_eSPI();

// ============================================================
// 电竞风配色方案 (黑底高对比度)
// ============================================================
#define COLOR_BG            TFT_BLACK
#define COLOR_PRIMARY       0x07FF    // 青色 Cyan
#define COLOR_SECONDARY     0xFFE0    // 黄色
#define COLOR_DANGER        0xF800    // 红色
#define COLOR_SUCCESS       0x07E0    // 绿色
#define COLOR_WARNING       0xFDA0    // 橙色
#define COLOR_TEXT          0xFFFF    // 白色
#define COLOR_TEXT_DIM      0x8410    // 灰色
#define COLOR_BAR_BG        0x18E3    // 深蓝灰
#define COLOR_CHARGE        0x07E0    // 充电绿
#define COLOR_DISCHARGE     0xF800    // 放电红

// ============================================================
// BMS 数据结构
// ============================================================
struct BMSData {
  float batteryVoltage = 0;      // 电池电压 V
  float batteryPower = 0;        // 电池功率 W
  float chargeCurrent = 0;       // 电流 A (正=充电,负=放电)
  float capacityRemain = 0;      // 剩余容量 Ah
  float nominalCapacity = 0;     // 总容量 Ah
  int   soc = 0;                 // 电量百分比 %
  float tempSensor1 = 0;         // 温度传感器1
  float tempSensor2 = 0;         // 温度传感器2
  float mosTemp = 0;             // MOS管温度
  bool  charging = false;        // 充电状态
  bool  discharging = false;     // 放电状态
  bool  balancing = false;       // 均衡状态
  bool  connected = false;       // 蓝牙连接状态
  unsigned long lastUpdate = 0;  // 最后更新时间
};

BMSData bmsData;

// ============================================================
// JKBMS BLE 类
// ============================================================
class JKBMS {
public:
  JKBMS(const std::string& mac) : targetMAC(mac) {}

  NimBLERemoteCharacteristic* pChr = nullptr;
  const NimBLEAdvertisedDevice* advDevice = nullptr;
  bool doConnect = false;
  bool connected = false;
  uint32_t lastNotifyTime = 0;
  std::string targetMAC;

  byte receivedBytes[MAX_RESPONSE_SIZE];
  int frame = 0;
  bool received_start = false;
  bool received_complete = false;
  bool new_data = false;

  bool connectToServer();
  void handleNotification(uint8_t* pData, size_t length);
  void parseCellInfo();
  void parseDeviceInfo();
  void writeRegister(uint8_t address, uint32_t value, uint8_t length);

private:
  uint8_t crc(const uint8_t data[], uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) crc += data[i];
    return crc;
  }
};

JKBMS jkBms(BMS_MAC_ADDRESS);
NimBLEScan* pScan;
unsigned long lastScanTime = 0;
unsigned long lastRequestTime = 0;

// ============================================================
// BLE 回调类
// ============================================================
class ClientCallbacks : public NimBLEClientCallbacks {
  JKBMS* bms;
public:
  ClientCallbacks(JKBMS* bmsInstance) : bms(bmsInstance) {}
  void onConnect(NimBLEClient* pClient) {
    DEBUG_PRINTF("Connected to %s\n", bms->targetMAC.c_str());
    bms->connected = true;
    bmsData.connected = true;
  }
  void onDisconnect(NimBLEClient* pClient, int reason) {
    DEBUG_PRINTF("%s disconnected, reason: %d\n", bms->targetMAC.c_str(), reason);
    bms->connected = false;
    bms->doConnect = false;
    bmsData.connected = false;
  }
};

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    DEBUG_PRINTF("BLE Device: %s\n", advertisedDevice->toString().c_str());
    if (advertisedDevice->getAddress().toString() == jkBms.targetMAC && !jkBms.connected && !jkBms.doConnect) {
      DEBUG_PRINTF("Found target BMS: %s\n", jkBms.targetMAC.c_str());
      jkBms.advDevice = advertisedDevice;
      jkBms.doConnect = true;
      NimBLEDevice::getScan()->stop();
    }
  }
} scanCallbacks;

void notifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
  jkBms.handleNotification(pData, length);
}

// ============================================================
// JKBMS 方法实现
// ============================================================
bool JKBMS::connectToServer() {
  DEBUG_PRINTF("Connecting to %s...\n", targetMAC.c_str());
  NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
  if (!pClient) {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCallbacks(this), true);
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5000);
  }
  if (!pClient->connect(advDevice)) {
    DEBUG_PRINTF("Failed to connect to %s\n", targetMAC.c_str());
    return false;
  }

  DEBUG_PRINTF("Connected: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
  NimBLERemoteService* pSvc = pClient->getService(JK_BMS_SERVICE_UUID);
  if (pSvc) {
    pChr = pSvc->getCharacteristic(JK_BMS_CHARACTERISTIC_UUID);
    if (pChr && pChr->canNotify()) {
      if (pChr->subscribe(true, notifyCB)) {
        DEBUG_PRINTLN("Subscribed to notifications");
        delay(500);
        writeRegister(COMMAND_DEVICE_INFO, 0x00000000, 0x00);
        delay(500);
        writeRegister(COMMAND_CELL_INFO, 0x00000000, 0x00);
        return true;
      }
    }
  }
  DEBUG_PRINTLN("Service/Characteristic not found");
  return false;
}

void JKBMS::writeRegister(uint8_t address, uint32_t value, uint8_t length) {
  uint8_t frame[20] = { 0xAA, 0x55, 0x90, 0xEB, address, length };
  frame[6] = value >> 0;
  frame[7] = value >> 8;
  frame[8] = value >> 16;
  frame[9] = value >> 24;
  frame[19] = crc(frame, 19);
  if (pChr) {
    pChr->writeValue((uint8_t*)frame, (size_t)sizeof(frame));
  }
}

void JKBMS::handleNotification(uint8_t* pData, size_t length) {
  lastNotifyTime = millis();
  if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
    frame = 0;
    received_start = true;
    received_complete = false;
    for (int i = 0; i < length; i++) {
      receivedBytes[frame++] = pData[i];
    }
  } else if (received_start && !received_complete) {
    for (int i = 0; i < length; i++) {
      receivedBytes[frame++] = pData[i];
      if (frame >= MIN_RESPONSE_SIZE) {
        received_complete = true;
        received_start = false;
        new_data = true;
        uint8_t crcCalc = crc(receivedBytes, frame - 1);
        if (crcCalc == receivedBytes[frame - 1]) {
          switch (receivedBytes[4]) {
            case 0x02: parseCellInfo(); break;
            case 0x03: parseDeviceInfo(); break;
          }
        }
        break;
      }
    }
  }
}

// JK02_32S 协议解析 - Cell Info
void JKBMS::parseCellInfo() {
  DEBUG_PRINTLN("Parsing Cell Info...");
  // 电池电压: bytes 118-121, uint32, 0.001V
  bmsData.batteryVoltage = ((receivedBytes[121] << 24) | (receivedBytes[120] << 16) | (receivedBytes[119] << 8) | receivedBytes[118]) * 0.001;
  // 电池功率: bytes 122-125, uint32, 0.001W
  bmsData.batteryPower = ((receivedBytes[125] << 24) | (receivedBytes[124] << 16) | (receivedBytes[123] << 8) | receivedBytes[122]) * 0.001;
  // 电流: bytes 126-129, int32, 0.001A
  int32_t currentRaw = (receivedBytes[129] << 24) | (receivedBytes[128] << 16) | (receivedBytes[127] << 8) | receivedBytes[126];
  bmsData.chargeCurrent = currentRaw * 0.001;
  // 温度传感器1: bytes 130-131, int16, 0.1C
  bmsData.tempSensor1 = ((int16_t)((receivedBytes[131] << 8) | receivedBytes[130])) * 0.1;
  // 温度传感器2: bytes 132-133, int16, 0.1C
  bmsData.tempSensor2 = ((int16_t)((receivedBytes[133] << 8) | receivedBytes[132])) * 0.1;
  // MOS温度: bytes 112-113, int16, 0.1C
  bmsData.mosTemp = ((int16_t)((receivedBytes[113] << 8) | receivedBytes[112])) * 0.1;
  // SOC: byte 141, uint8, 1%
  bmsData.soc = receivedBytes[141];
  // 剩余容量: bytes 142-145, uint32, 0.001Ah
  bmsData.capacityRemain = ((receivedBytes[145] << 24) | (receivedBytes[144] << 16) | (receivedBytes[143] << 8) | receivedBytes[142]) * 0.001;
  // 总容量: bytes 146-149, uint32, 0.001Ah
  bmsData.nominalCapacity = ((receivedBytes[149] << 24) | (receivedBytes[148] << 16) | (receivedBytes[147] << 8) | receivedBytes[146]) * 0.001;
  // 均衡电流: bytes 138-139, int16, 0.001A
  bmsData.balancing = (receivedBytes[140] != 0);
  // 充放电状态
  bmsData.charging = (receivedBytes[166] != 0);
  bmsData.discharging = (receivedBytes[167] != 0);

  bmsData.lastUpdate = millis();
  DEBUG_PRINTF("V:%.2fV P:%.1fW I:%.2fA SOC:%d%% T1:%.1fC\n",
               bmsData.batteryVoltage, bmsData.batteryPower, bmsData.chargeCurrent, bmsData.soc, bmsData.tempSensor1);
}

void JKBMS::parseDeviceInfo() {
  DEBUG_PRINTLN("Parsing Device Info...");
}

// ============================================================
// TFT 显示函数 - 电竞风UI
// ============================================================

void drawProgressBar(int x, int y, int w, int h, int percent, uint16_t color) {
  tft.drawRoundRect(x, y, w, h, 3, COLOR_TEXT_DIM);
  int fillW = (w - 4) * percent / 100;
  if (fillW > 0) {
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, 2, color);
  }
}

void drawGradientBar(int x, int y, int w, int h, int percent) {
  tft.drawRoundRect(x, y, w, h, 3, COLOR_TEXT_DIM);
  int fillW = (w - 4) * percent / 100;
  for (int i = 0; i < fillW; i++) {
    uint16_t gradColor;
    int gradPos = i * 255 / (w - 4);
    if (gradPos < 128) {
      gradColor = tft.color565(255 - gradPos * 2, gradPos * 2, 0);
    } else {
      gradColor = tft.color565(0, 255 - (gradPos - 128) * 2, (gradPos - 128) * 2);
    }
    tft.drawFastVLine(x + 2 + i, y + 2, h - 4, gradColor);
  }
}

void drawCenteredText(const char* text, int x, int y, int font, uint16_t color) {
  tft.setTextFont(font);
  tft.setTextColor(color, COLOR_BG);
  int16_t cx = tft.textWidth(text) / 2;
  tft.setCursor(x - cx, y);
  tft.print(text);
}

void drawRightAlignedText(const char* text, int x, int y, int font, uint16_t color) {
  tft.setTextFont(font);
  tft.setTextColor(color, COLOR_BG);
  int16_t tw = tft.textWidth(text);
  tft.setCursor(x - tw, y);
  tft.print(text);
}

void drawLabelValue(const char* label, float value, const char* unit, int x, int y, uint16_t valueColor) {
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(x, y);
  tft.print(label);

  char buf[32];
  dtostrf(value, 4, 1, buf);
  strcat(buf, unit);

  tft.setTextFont(4);
  tft.setTextColor(valueColor, COLOR_BG);
  tft.setCursor(x + 80, y - 4);
  tft.print(buf);
}

// ============================================================
// 主界面绘制
// ============================================================
void drawMainScreen() {
  tft.fillScreen(COLOR_BG);

  // === 顶部标题栏 ===
  tft.fillRect(0, 0, 320, 22, 0x1082);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_PRIMARY, 0x1082);
  tft.setCursor(8, 4);
  tft.print("JK-BMS ");
  tft.print(BMS_DEVICE_NAME);

  // 连接状态指示
  if (bmsData.connected) {
    tft.fillCircle(305, 11, 5, COLOR_SUCCESS);
  } else {
    tft.fillCircle(305, 11, 5, COLOR_DANGER);
  }

  // === 主视觉区: 功率显示 (占据上半部分) ===
  int centerX = 160;
  int powerY = 45;

  // 功率标签
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(centerX - 35, powerY);
  tft.print("POWER");

  // 功率大数字
  char powerStr[16];
  float displayPower = abs(bmsData.batteryPower);
  if (displayPower >= 1000) {
    dtostrf(displayPower / 1000.0, 4, 2, powerStr);
    strcat(powerStr, " kW");
  } else {
    dtostrf(displayPower, 4, 1, powerStr);
    strcat(powerStr, " W");
  }

  tft.setTextFont(7);
  uint16_t powerColor = (bmsData.batteryPower >= 0) ? COLOR_CHARGE : COLOR_DISCHARGE;
  if (!bmsData.connected) powerColor = COLOR_TEXT_DIM;
  tft.setTextColor(powerColor, COLOR_BG);
  int pw = tft.textWidth(powerStr);
  tft.setCursor(centerX - pw / 2, powerY + 16);
  tft.print(powerStr);

  // 充放电指示
  tft.setTextFont(2);
  if (bmsData.batteryPower > 10) {
    tft.setTextColor(COLOR_CHARGE, COLOR_BG);
    drawCenteredText("CHARGING", centerX, powerY + 70, 2, COLOR_CHARGE);
  } else if (bmsData.batteryPower < -10) {
    tft.setTextColor(COLOR_DISCHARGE, COLOR_BG);
    drawCenteredText("DISCHARGING", centerX, powerY + 70, 2, COLOR_DISCHARGE);
  } else {
    drawCenteredText("STANDBY", centerX, powerY + 70, 2, COLOR_TEXT_DIM);
  }

  // === 电压电流信息条 ===
  int infoY = 125;
  tft.drawFastHLine(10, infoY, 300, 0x2104);

  // 电压
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(20, infoY + 8);
  tft.print("VOLT");
  char voltStr[16];
  dtostrf(bmsData.batteryVoltage, 4, 2, voltStr);
  strcat(voltStr, "V");
  tft.setTextFont(4);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.setCursor(20, infoY + 24);
  tft.print(voltStr);

  // 电流
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(130, infoY + 8);
  tft.print("CURRENT");
  char currStr[16];
  dtostrf(abs(bmsData.chargeCurrent), 4, 2, currStr);
  strcat(currStr, "A");
  tft.setTextFont(4);
  uint16_t currColor = (bmsData.chargeCurrent >= 0) ? COLOR_CHARGE : COLOR_DISCHARGE;
  if (!bmsData.connected) currColor = COLOR_TEXT_DIM;
  tft.setTextColor(currColor, COLOR_BG);
  tft.setCursor(130, infoY + 24);
  tft.print(currStr);

  // SOC 百分比大数字
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(250, infoY + 8);
  tft.print("SOC");
  char socStr[8];
  itoa(bmsData.soc, socStr, 10);
  strcat(socStr, "%");
  tft.setTextFont(4);
  uint16_t socColor = (bmsData.soc > 20) ? COLOR_PRIMARY : COLOR_DANGER;
  if (!bmsData.connected) socColor = COLOR_TEXT_DIM;
  tft.setTextColor(socColor, COLOR_BG);
  tft.setCursor(250, infoY + 24);
  tft.print(socStr);

  // === 容量进度条区 ===
  int capY = 195;

  // 容量标签
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(15, capY);
  tft.print("CAPACITY");

  // 容量数值
  char capStr[32];
  dtostrf(bmsData.capacityRemain, 4, 1, capStr);
  strcat(capStr, " / ");
  char nomStr[16];
  dtostrf(bmsData.nominalCapacity, 4, 1, nomStr);
  strcat(capStr, nomStr);
  strcat(capStr, " Ah");

  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(100, capY);
  tft.print(capStr);

  // 进度条背景
  drawProgressBar(15, capY + 18, 290, 14, bmsData.soc, COLOR_PRIMARY);

  // === 底部温度区 ===
  int tempY = 245;
  tft.drawFastHLine(10, tempY, 300, 0x2104);

  // 温度图标和数值
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(20, tempY + 8);
  tft.print("TEMP");

  char t1Str[16], t2Str[16], mosStr[16];
  dtostrf(bmsData.tempSensor1, 4, 1, t1Str);
  strcat(t1Str, "C");
  dtostrf(bmsData.tempSensor2, 4, 1, t2Str);
  strcat(t2Str, "C");
  dtostrf(bmsData.mosTemp, 4, 1, mosStr);
  strcat(mosStr, "C");

  // T1
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(80, tempY + 4);
  tft.print("T1:");
  tft.setTextColor((bmsData.tempSensor1 > 45) ? COLOR_WARNING : COLOR_TEXT, COLOR_BG);
  tft.print(t1Str);

  // T2
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(170, tempY + 4);
  tft.print("T2:");
  tft.setTextColor((bmsData.tempSensor2 > 45) ? COLOR_WARNING : COLOR_TEXT, COLOR_BG);
  tft.print(t2Str);

  // MOS
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.setCursor(250, tempY + 4);
  tft.print("MOS:");
  tft.setTextColor((bmsData.mosTemp > 60) ? COLOR_DANGER : COLOR_TEXT, COLOR_BG);
  tft.print(mosStr);

  // === 底部状态栏 ===
  tft.fillRect(0, 298, 320, 22, 0x1082);
  tft.setTextFont(1);
  tft.setTextColor(COLOR_TEXT_DIM, 0x1082);
  tft.setCursor(8, 303);

  if (bmsData.connected) {
    if (bmsData.charging) tft.print("[CHG] ");
    if (bmsData.discharging) tft.print("[DIS] ");
    if (bmsData.balancing) tft.print("[BAL] ");
    unsigned long age = (millis() - bmsData.lastUpdate) / 1000;
    tft.printf("Update: %lus ago", age);
  } else {
    tft.print("BLE DISCONNECTED - SCANNING...");
  }
}

// ============================================================
// 启动画面
// ============================================================
void drawBootScreen() {
  tft.fillScreen(COLOR_BG);

  // 外框
  tft.drawRect(10, 10, 300, 300, COLOR_PRIMARY);
  tft.drawRect(12, 12, 296, 296, COLOR_PRIMARY);

  // 标题
  tft.setTextFont(4);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  drawCenteredText("JK-BMS MONITOR", 160, 80, 4, COLOR_PRIMARY);

  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  drawCenteredText("ESP32 BLE DASHBOARD", 160, 115, 2, COLOR_TEXT_DIM);

  // 设备信息
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(60, 160);
  tft.print("Device: ");
  tft.print(BMS_DEVICE_NAME);

  tft.setCursor(60, 185);
  tft.print("MAC: ");
  tft.print(BMS_MAC_ADDRESS);

  // 加载动画
  for (int i = 0; i < 3; i++) {
    tft.fillCircle(140 + i * 20, 240, 6, COLOR_PRIMARY);
    delay(200);
  }

  tft.setTextColor(COLOR_SUCCESS, COLOR_BG);
  drawCenteredText("INITIALIZING...", 160, 270, 2, COLOR_SUCCESS);
}

// ============================================================
// Setup & Loop
// ============================================================
void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("\n=== JK-BMS Display Starting ===");

  // 初始化TFT
  tft.init();
  tft.setRotation(1); // 横屏

  // 开启背光 (IO21, 高电平点亮)
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tft.fillScreen(COLOR_BG);

  drawBootScreen();

  // 初始化BLE
  NimBLEDevice::init("JK-BMS-Display");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(100);
  pScan->setWindow(80);
  pScan->setActiveScan(true);

  delay(1000);
  tft.fillScreen(COLOR_BG);
}

void loop() {
  // BLE连接管理
  if (!jkBms.connected && !jkBms.doConnect) {
    if (millis() - lastScanTime > 5000) {
      DEBUG_PRINTLN("Scanning for BMS...");
      pScan->start(3);
      lastScanTime = millis();
    }
  } else if (jkBms.doConnect && !jkBms.connected) {
    if (jkBms.connectToServer()) {
      jkBms.doConnect = false;
    } else {
      jkBms.doConnect = false;
      delay(2000);
    }
  }

  // 定期请求数据
  if (jkBms.connected && jkBms.pChr) {
    if (millis() - lastRequestTime > 3000) {
      jkBms.writeRegister(COMMAND_CELL_INFO, 0x00000000, 0x00);
      lastRequestTime = millis();
    }
  }

  // 绘制界面
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw > 500) {
    drawMainScreen();
    lastDraw = millis();
  }

  delay(100);
}
