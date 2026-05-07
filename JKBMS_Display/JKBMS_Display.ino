#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ============================================================
// JKBMS BLE 监控 - 电竞风仪表盘
// 开发板: ESP32-32E
// 显示屏: 2.8寸 ST7789P3 (240x320)
// BMS MAC: 98:DA:20:07:B9:00
// BMS名称: JK_BD4A24S10P
// 使用 ESP32 原生 BLE 库 (无需 NimBLE)
// ============================================================

// ----- 调试开关 -----
#define DEBUG_ENABLED true
#if DEBUG_ENABLED
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// ----- BMS配置 -----
#define BMS_MAC_ADDRESS "98:da:20:07:b9:00"
#define BMS_NAME "JK_BD4A24S10P"

// ----- BLE服务UUID -----
static BLEUUID serviceUUID("ffe0");
static BLEUUID charUUID("ffe1");

// ----- 显示屏配置 -----
TFT_eSPI tft = TFT_eSPI();

// ----- 颜色定义 (电竞风黑底高对比度) -----
#define COLOR_BG        0x0000
#define COLOR_PRIMARY   0x07E0
#define COLOR_SECONDARY 0x001F
#define COLOR_ACCENT    0xF800
#define COLOR_WARNING   0xFFE0
#define COLOR_TEXT      0xFFFF
#define COLOR_DIM       0x4208
#define COLOR_GRID      0x1082

// ----- 屏幕尺寸 -----
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// ----- 背光引脚 -----
#define BACKLIGHT_PIN 21

// ----- 数据刷新间隔 -----
#define DISPLAY_REFRESH_MS 500
#define BMS_POLL_MS 2000

// ============================================================
// JKBMS 数据结构
// ============================================================
struct BMSData {
  bool valid = false;
  bool connected = false;
  float batteryVoltage = 0;
  float chargeCurrent = 0;
  float batteryPower = 0;
  int soc = 0;
  float capacityRemain = 0;
  float nominalCapacity = 0;
  float tempT1 = 0;
  float tempT2 = 0;
  float tempMOS = 0;
  float cellVoltage[24] = {0};
  int cellCount = 0;
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  bool charging = false;
  bool discharging = false;
  bool balancing = false;
  float cycleCount = 0;
  float cycleCapacity = 0;
  unsigned long lastUpdate = 0;
};

BMSData bmsData;

// ============================================================
// JKBMS BLE 类 (使用 ESP32 原生 BLE)
// ============================================================
class JKBMS_BLE : public BLEClientCallbacks, public BLEAdvertisedDeviceCallbacks {
public:
  JKBMS_BLE(const char* mac) : targetMAC(mac) {}

  bool begin() {
    BLEDevice::init("");
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(this);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->setActiveScan(true);
    pScan->start(5, false);
    DEBUG_PRINTLN("BLE扫描已启动...");
    return true;
  }

  void loop() {
    if (!connected && !doConnect) {
      if (millis() - lastScanTime > 10000) {
        BLEScan* pScan = BLEDevice::getScan();
        pScan->start(5, false);
        lastScanTime = millis();
      }
    }

    if (doConnect && !connected) {
      if (connectToServer()) {
        connected = true;
        doConnect = false;
        bmsData.connected = true;
      } else {
        doConnect = false;
        delay(2000);
      }
    }

    if (connected && millis() - lastPollTime > BMS_POLL_MS) {
      requestCellInfo();
      lastPollTime = millis();
    }

    if (connected && millis() - lastNotifyTime > 15000) {
      DEBUG_PRINTLN("连接超时，断开重连");
      connected = false;
      bmsData.connected = false;
    }
  }

  bool isConnected() { return connected; }

  // BLEAdvertisedDeviceCallbacks
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    std::string addr = advertisedDevice.getAddress().toString();
    if (addr == targetMAC) {
      DEBUG_PRINTF("找到BMS: %s\n", addr.c_str());
      advDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      BLEDevice::getScan()->stop();
    }
  }

  // BLEClientCallbacks
  void onConnect(BLEClient* pClient) override {
    DEBUG_PRINTLN("BLE已连接");
  }

  void onDisconnect(BLEClient* pClient) override {
    DEBUG_PRINTLN("BLE已断开");
    connected = false;
    doConnect = false;
    bmsData.connected = false;
  }

  void notifyCallback(BLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
    handleNotification(pData, length);
  }

private:
  const char* targetMAC;
  BLERemoteCharacteristic* pChr = nullptr;
  BLEAdvertisedDevice* advDevice = nullptr;
  BLEClient* pClient = nullptr;
  bool doConnect = false;
  bool connected = false;
  unsigned long lastScanTime = 0;
  unsigned long lastPollTime = 0;
  unsigned long lastNotifyTime = 0;

  byte rxBuffer[320];
  int rxIndex = 0;
  bool rxActive = false;

  bool connectToServer() {
    DEBUG_PRINTF("连接BMS: %s\n", targetMAC);
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(this);

    if (!pClient->connect(advDevice)) {
      DEBUG_PRINTLN("连接失败");
      return false;
    }

    BLERemoteService* pSvc = pClient->getService(serviceUUID);
    if (!pSvc) {
      DEBUG_PRINTLN("服务未找到");
      pClient->disconnect();
      return false;
    }

    pChr = pSvc->getCharacteristic(charUUID);
    if (!pChr || !pChr->canNotify()) {
      DEBUG_PRINTLN("特征值未找到");
      pClient->disconnect();
      return false;
    }

    if (!pChr->subscribe(true, [this](BLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
      this->notifyCallback(pChr, pData, length, isNotify);
    })) {
      DEBUG_PRINTLN("订阅失败");
      pClient->disconnect();
      return false;
    }

    DEBUG_PRINTLN("BLE连接成功，已订阅通知");
    delay(500);
    requestDeviceInfo();
    delay(500);
    requestCellInfo();
    return true;
  }

  void requestDeviceInfo() {
    sendCommand(0x97, 0, 0);
  }

  void requestCellInfo() {
    sendCommand(0x96, 0, 0);
  }

  void sendCommand(uint8_t cmd, uint32_t value, uint8_t len) {
    uint8_t frame[20] = {0xAA, 0x55, 0x90, 0xEB, cmd, len};
    frame[6] = value & 0xFF;
    frame[7] = (value >> 8) & 0xFF;
    frame[8] = (value >> 16) & 0xFF;
    frame[9] = (value >> 24) & 0xFF;

    uint8_t crc = 0;
    for (int i = 0; i < 19; i++) crc += frame[i];
    frame[19] = crc;

    if (pChr) {
      pChr->writeValue(frame, 20, false);
    }
  }

  void handleNotification(uint8_t* pData, size_t length) {
    lastNotifyTime = millis();

    for (size_t i = 0; i < length; i++) {
      if (!rxActive && pData[i] == 0x55) {
        if (i + 3 < length && pData[i+1] == 0xAA && pData[i+2] == 0xEB && pData[i+3] == 0x90) {
          rxActive = true;
          rxIndex = 0;
          for (int j = 0; j < 4 && (size_t)(i + j) < length; j++) {
            rxBuffer[rxIndex++] = pData[i + j];
          }
        }
      } else if (rxActive) {
        rxBuffer[rxIndex++] = pData[i];
        if (rxIndex >= 300) {
          rxActive = false;
          parseFrame();
          rxIndex = 0;
        }
      }
    }
  }

  void parseFrame() {
    uint8_t frameType = rxBuffer[4];
    switch (frameType) {
      case 0x02: parseCellInfo(); break;
      case 0x03: parseDeviceInfo(); break;
      default: break;
    }
  }

  void parseCellInfo() {
    bmsData.cellCount = 0;
    float sum = 0;
    float maxV = 0, minV = 999;

    for (int i = 0; i < 24; i++) {
      int offset = 6 + i * 2;
      uint16_t raw = rxBuffer[offset] | (rxBuffer[offset + 1] << 8);
      float v = raw * 0.001;
      if (v > 0) {
        bmsData.cellVoltage[i] = v;
        bmsData.cellCount++;
        sum += v;
        if (v > maxV) maxV = v;
        if (v < minV) minV = v;
      }
    }

    if (bmsData.cellCount > 0) {
      bmsData.avgCellVoltage = sum / bmsData.cellCount;
      bmsData.deltaCellVoltage = maxV - minV;
    }

    bmsData.batteryVoltage = (rxBuffer[118] | (rxBuffer[119] << 8) |
                              (rxBuffer[120] << 16) | (rxBuffer[121] << 24)) * 0.001;

    bmsData.batteryPower = (rxBuffer[122] | (rxBuffer[123] << 8) |
                            (rxBuffer[124] << 16) | (rxBuffer[125] << 24)) * 0.001;

    int32_t currentRaw = rxBuffer[126] | (rxBuffer[127] << 8) |
                         (rxBuffer[128] << 16) | (rxBuffer[129] << 24);
    bmsData.chargeCurrent = currentRaw * 0.001;

    bmsData.tempMOS = (int16_t)(rxBuffer[112] | (rxBuffer[113] << 8)) * 0.1;
    bmsData.tempT1 = (int16_t)(rxBuffer[130] | (rxBuffer[131] << 8)) * 0.1;
    bmsData.tempT2 = (int16_t)(rxBuffer[132] | (rxBuffer[133] << 8)) * 0.1;

    bmsData.balancing = (rxBuffer[140] != 0);
    bmsData.soc = rxBuffer[141];

    bmsData.capacityRemain = (rxBuffer[142] | (rxBuffer[143] << 8) |
                              (rxBuffer[144] << 16) | (rxBuffer[145] << 24)) * 0.001;

    bmsData.nominalCapacity = (rxBuffer[146] | (rxBuffer[147] << 8) |
                               (rxBuffer[148] << 16) | (rxBuffer[149] << 24)) * 0.001;

    bmsData.cycleCount = rxBuffer[150] | (rxBuffer[151] << 8) |
                         (rxBuffer[152] << 16) | (rxBuffer[153] << 24);

    bmsData.cycleCapacity = (rxBuffer[154] | (rxBuffer[155] << 8) |
                             (rxBuffer[156] << 16) | (rxBuffer[157] << 24)) * 0.001;

    bmsData.charging = (bmsData.chargeCurrent > 0.1);
    bmsData.discharging = (bmsData.chargeCurrent < -0.1);
    bmsData.valid = true;
    bmsData.lastUpdate = millis();

    DEBUG_PRINTF("电压:%.2fV 电流:%.2fA 功率:%.1fW SOC:%d%% 温度:%.1fC\n",
                 bmsData.batteryVoltage, bmsData.chargeCurrent,
                 bmsData.batteryPower, bmsData.soc, bmsData.tempT1);
  }

  void parseDeviceInfo() {
    DEBUG_PRINTLN("收到设备信息帧");
  }
};

JKBMS_BLE jkbms(BMS_MAC_ADDRESS);

// ============================================================
// UI 显示类 - 电竞风仪表盘
// ============================================================
class DashboardUI {
public:
  void begin() {
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    drawFrame();
  }

  void update() {
    if (!bmsData.valid) {
      drawConnecting();
      return;
    }
    drawPower();
    drawCapacity();
    drawVoltageCurrent();
    drawTemperature();
    drawStatus();
  }

private:
  float lastPower = -999;
  int lastSOC = -1;
  float lastVoltage = -1;
  float lastCurrent = -999;
  float lastTemp = -999;
  float lastCapacity = -1;
  float lastTotalCapacity = -1;

  void drawFrame() {
    tft.fillScreen(COLOR_BG);
    tft.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_DIM);
    tft.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_PRIMARY);
    tft.setTextColor(COLOR_TEXT, COLOR_DIM);
    tft.setTextSize(1);
    tft.setFreeFont(nullptr);
    tft.drawString("JK-BMS MONITOR", SCREEN_WIDTH / 2, 14);
    tft.drawFastHLine(10, 155, SCREEN_WIDTH - 20, COLOR_GRID);
    tft.drawFastHLine(10, 235, SCREEN_WIDTH - 20, COLOR_GRID);
    tft.fillRect(0, 295, SCREEN_WIDTH, 25, COLOR_DIM);
    tft.drawFastHLine(0, 295, SCREEN_WIDTH, COLOR_SECONDARY);
  }

  void drawConnecting() {
    static int dotCount = 0;
    static unsigned long lastDot = 0;

    if (millis() - lastDot > 500) {
      dotCount = (dotCount + 1) % 4;
      lastDot = millis();

      tft.fillRect(20, 80, SCREEN_WIDTH - 40, 40, COLOR_BG);
      tft.setTextColor(COLOR_SECONDARY, COLOR_BG);
      tft.setTextSize(2);

      String msg = "BLE连接中";
      for (int i = 0; i < dotCount; i++) msg += ".";
      tft.drawString(msg, SCREEN_WIDTH / 2, 100);

      tft.setTextSize(1);
      tft.setTextColor(COLOR_DIM, COLOR_BG);
      tft.drawString(BMS_MAC_ADDRESS, SCREEN_WIDTH / 2, 140);
      tft.drawString(BMS_NAME, SCREEN_WIDTH / 2, 158);
    }
  }

  void drawPower() {
    float power = bmsData.batteryPower;
    bool isDischarge = bmsData.discharging;

    if (abs(power - lastPower) < 1 && lastSOC == bmsData.soc) return;
    lastPower = power;
    lastSOC = bmsData.soc;

    tft.fillRect(0, 32, SCREEN_WIDTH, 120, COLOR_BG);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString(isDischarge ? "DISCHARGE POWER" : "CHARGE POWER", SCREEN_WIDTH / 2, 44);

    uint16_t powerColor = isDischarge ? COLOR_ACCENT : COLOR_PRIMARY;
    if (power < 10) powerColor = COLOR_DIM;

    char powerStr[16];
    if (power >= 1000) {
      sprintf(powerStr, "%.2f", power / 1000.0);
      tft.setTextColor(powerColor, COLOR_BG);
      tft.setTextSize(3);
      tft.drawString(powerStr, SCREEN_WIDTH / 2 - 20, 85);
      tft.setTextSize(2);
      tft.setTextColor(COLOR_DIM, COLOR_BG);
      tft.drawString("kW", SCREEN_WIDTH / 2 + 55, 85);
    } else {
      sprintf(powerStr, "%.0f", power);
      tft.setTextColor(powerColor, COLOR_BG);
      tft.setTextSize(4);
      tft.drawString(powerStr, SCREEN_WIDTH / 2 - 15, 85);
      tft.setTextSize(2);
      tft.setTextColor(COLOR_DIM, COLOR_BG);
      tft.drawString("W", SCREEN_WIDTH / 2 + 55, 85);
    }

    int barWidth = SCREEN_WIDTH - 40;
    int barX = 20;
    int barY = 125;
    int barHeight = 8;

    float maxPower = bmsData.nominalCapacity > 0 ? bmsData.nominalCapacity * 3.7 * 0.5 : 500;
    int fillWidth = (int)(barWidth * min(power / maxPower, 1.0));

    tft.drawRect(barX, barY, barWidth, barHeight, COLOR_GRID);
    tft.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, COLOR_BG);

    if (fillWidth > 0) {
      uint16_t barColor = isDischarge ? COLOR_ACCENT : COLOR_PRIMARY;
      for (int i = 0; i < fillWidth - 4; i++) {
        uint16_t c = barColor;
        if (i > fillWidth * 0.7) c = COLOR_WARNING;
        tft.drawFastVLine(barX + 2 + i, barY + 2, barHeight - 4, c);
      }
    }

    drawSOCRing(190, 70, 22, bmsData.soc);
  }

  void drawSOCRing(int cx, int cy, int r, int soc) {
    for (int i = 0; i < 360; i += 6) {
      float rad = i * PI / 180;
      int x1 = cx + (r - 3) * cos(rad);
      int y1 = cy + (r - 3) * sin(rad);
      int x2 = cx + r * cos(rad);
      int y2 = cy + r * sin(rad);
      tft.drawLine(x1, y1, x2, y2, COLOR_DIM);
    }

    int endAngle = soc * 360 / 100;
    uint16_t socColor = soc > 50 ? COLOR_PRIMARY : (soc > 20 ? COLOR_WARNING : COLOR_ACCENT);

    for (int i = -90; i < -90 + endAngle; i += 3) {
      float rad = i * PI / 180;
      int x1 = cx + (r - 3) * cos(rad);
      int y1 = cy + (r - 3) * sin(rad);
      int x2 = cx + r * cos(rad);
      int y2 = cy + r * sin(rad);
      tft.drawLine(x1, y1, x2, y2, socColor);
    }

    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(1);
    char socStr[8];
    sprintf(socStr, "%d%%", soc);
    tft.drawString(socStr, cx, cy);
  }

  void drawCapacity() {
    float remain = bmsData.capacityRemain;
    float total = bmsData.nominalCapacity;

    if (abs(remain - lastCapacity) < 0.01 && total == lastTotalCapacity) return;
    lastCapacity = remain;
    lastTotalCapacity = total;

    tft.fillRect(0, 160, SCREEN_WIDTH, 72, COLOR_BG);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("BATTERY CAPACITY", SCREEN_WIDTH / 2, 168);

    char capStr[32];
    if (total > 0) {
      sprintf(capStr, "%.1f / %.1f", remain, total);
    } else {
      sprintf(capStr, "%.1f Ah", remain);
    }

    tft.setTextColor(COLOR_SECONDARY, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString(capStr, SCREEN_WIDTH / 2, 192);

    tft.setTextSize(1);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.drawString("Ah", SCREEN_WIDTH / 2, 212);

    int barWidth = SCREEN_WIDTH - 40;
    int barX = 20;
    int barY = 222;
    int barHeight = 6;

    int fillWidth = total > 0 ? (int)(barWidth * remain / total) : 0;

    tft.drawRect(barX, barY, barWidth, barHeight, COLOR_GRID);
    tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, COLOR_BG);

    if (fillWidth > 0) {
      tft.fillRect(barX + 1, barY + 1, fillWidth - 2, barHeight - 2, COLOR_SECONDARY);
    }
  }

  void drawVoltageCurrent() {
    float voltage = bmsData.batteryVoltage;
    float current = bmsData.chargeCurrent;

    if (abs(voltage - lastVoltage) < 0.01 && abs(current - lastCurrent) < 0.01) return;
    lastVoltage = voltage;
    lastCurrent = current;

    tft.fillRect(0, 240, SCREEN_WIDTH, 52, COLOR_BG);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("VOLTAGE", 60, 248);

    char vStr[16];
    sprintf(vStr, "%.2fV", voltage);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString(vStr, 60, 268);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("CURRENT", 180, 248);

    char cStr[16];
    sprintf(cStr, "%.2fA", current);
    uint16_t cColor = current < -0.1 ? COLOR_ACCENT : (current > 0.1 ? COLOR_PRIMARY : COLOR_TEXT);
    tft.setTextColor(cColor, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString(cStr, 180, 268);
  }

  void drawTemperature() {
    float t1 = bmsData.tempT1;
    float t2 = bmsData.tempT2;
    float tmos = bmsData.tempMOS;
    float maxTemp = max(t1, max(t2, tmos));

    if (abs(maxTemp - lastTemp) < 0.5) return;
    lastTemp = maxTemp;

    tft.fillRect(0, 296, SCREEN_WIDTH, 24, COLOR_DIM);

    char tempStr[32];
    sprintf(tempStr, "T:%.0fC", maxTemp);

    uint16_t tColor = maxTemp > 55 ? COLOR_ACCENT : (maxTemp > 45 ? COLOR_WARNING : COLOR_PRIMARY);
    tft.setTextColor(tColor, COLOR_DIM);
    tft.setTextSize(1);
    tft.drawString(tempStr, 45, 308);

    char cellStr[16];
    sprintf(cellStr, "%dS", bmsData.cellCount);
    tft.setTextColor(COLOR_TEXT, COLOR_DIM);
    tft.drawString(cellStr, 120, 308);

    char cycleStr[16];
    sprintf(cycleStr, "Cyc:%.0f", bmsData.cycleCount);
    tft.setTextColor(COLOR_TEXT, COLOR_DIM);
    tft.drawString(cycleStr, 195, 308);
  }

  void drawStatus() {
    static bool lastCharge = false;
    static bool lastDischarge = false;
    static bool lastBalance = false;

    if (lastCharge == bmsData.charging && lastDischarge == bmsData.discharging &&
        lastBalance == bmsData.balancing) return;

    lastCharge = bmsData.charging;
    lastDischarge = bmsData.discharging;
    lastBalance = bmsData.balancing;

    int y = 14;
    tft.fillCircle(185, y, 4, bmsData.charging ? COLOR_PRIMARY : COLOR_DIM);
    tft.fillCircle(200, y, 4, bmsData.discharging ? COLOR_ACCENT : COLOR_DIM);
    tft.fillCircle(215, y, 4, bmsData.balancing ? COLOR_WARNING : COLOR_DIM);
  }
};

DashboardUI dashboard;

// ============================================================
// 系统初始化
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  DEBUG_PRINTLN("\n================================");
  DEBUG_PRINTLN("  JKBMS 电竞风监控仪表盘");
  DEBUG_PRINTLN("================================");

  dashboard.begin();

  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.setTextSize(2);
  tft.drawString("JKBMS", SCREEN_WIDTH / 2, 120);
  tft.setTextColor(COLOR_SECONDARY, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("Initializing BLE...", SCREEN_WIDTH / 2, 150);

  if (!jkbms.begin()) {
    DEBUG_PRINTLN("BLE初始化失败");
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.drawString("BLE Init Failed!", SCREEN_WIDTH / 2, 180);
  }

  delay(1000);
  tft.fillScreen(COLOR_BG);
  dashboard.update();
}

// ============================================================
// 主循环
// ============================================================
void loop() {
  jkbms.loop();

  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > DISPLAY_REFRESH_MS) {
    dashboard.update();
    lastDisplayUpdate = millis();
  }

  delay(10);
}
