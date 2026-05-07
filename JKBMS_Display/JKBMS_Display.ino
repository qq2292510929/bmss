#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ============================================================
// JKBMS BLE 监控 - 电竞风仪表盘
// 开发板: ESP32-32E
// 显示屏: 2.8寸 ST7789P3 (240x320)
// BMS MAC: 98:DA:20:07:B9:00
// BMS名称: JK_BD4A24S10P
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
#define BMS_MAC_ADDRESS "98:DA:20:07:B9:00"
#define BMS_NAME "JK_BD4A24S10P"

// ----- BLE服务UUID -----
#define SERVICE_UUID "ffe0"
#define CHAR_UUID "ffe1"

// ----- 显示屏配置 -----
TFT_eSPI tft = TFT_eSPI();

// ----- 颜色定义 (电竞风黑底高对比度) -----
#define COLOR_BG        0x0000   // 纯黑背景
#define COLOR_PRIMARY   0x07E0   // 霓虹绿 #00FF41
#define COLOR_SECONDARY 0x001F   // 电光蓝 #00BFFF
#define COLOR_ACCENT    0xF800   // 警示红 #FF0040
#define COLOR_WARNING   0xFFE0   // 琥珀黄 #FFC800
#define COLOR_TEXT      0xFFFF   // 纯白
#define COLOR_DIM       0x4208   // 暗灰 #444444
#define COLOR_GRID      0x1082   // 网格蓝灰

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

  // 核心数据
  float batteryVoltage = 0;      // 电池电压 V
  float chargeCurrent = 0;       // 充电电流 A (负值为放电)
  float batteryPower = 0;        // 电池功率 W
  int soc = 0;                   // 电量百分比 %
  float capacityRemain = 0;      // 剩余容量 Ah
  float nominalCapacity = 0;     // 标称容量 Ah

  // 温度
  float tempT1 = 0;              // 温度传感器1
  float tempT2 = 0;              // 温度传感器2
  float tempMOS = 0;             // MOS管温度

  // 单体电芯
  float cellVoltage[24] = {0};   // 最多24串
  int cellCount = 0;
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;

  // 状态
  bool charging = false;
  bool discharging = false;
  bool balancing = false;

  // 循环
  float cycleCount = 0;
  float cycleCapacity = 0;

  unsigned long lastUpdate = 0;
};

BMSData bmsData;

// ============================================================
// JKBMS BLE 类
// ============================================================
class JKBMS_BLE {
public:
  JKBMS_BLE(const char* mac) : targetMAC(mac) {}

  bool begin() {
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCallbacks, false);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->setActiveScan(true);
    pScan->start(5, nullptr, false);
    DEBUG_PRINTLN("BLE扫描已启动...");
    return true;
  }

  void loop() {
    if (!connected && !doConnect) {
      if (millis() - lastScanTime > 10000) {
        pScan->start(5, nullptr, false);
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

private:
  const char* targetMAC;
  NimBLERemoteCharacteristic* pChr = nullptr;
  const NimBLEAdvertisedDevice* advDevice = nullptr;
  bool doConnect = false;
  bool connected = false;
  NimBLEScan* pScan;
  unsigned long lastScanTime = 0;
  unsigned long lastPollTime = 0;
  unsigned long lastNotifyTime = 0;

  byte rxBuffer[320];
  int rxIndex = 0;
  bool rxActive = false;

  static void notifyCallback(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify);

  class ScanCallbacks : public NimBLEScanCallbacks {
  public:
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice);
  } scanCallbacks;

  bool connectToServer() {
    DEBUG_PRINTF("连接BMS: %s\n", targetMAC);
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5000);

    if (!pClient->connect(advDevice)) {
      DEBUG_PRINTLN("连接失败");
      return false;
    }

    NimBLERemoteService* pSvc = pClient->getService(SERVICE_UUID);
    if (!pSvc) {
      DEBUG_PRINTLN("服务未找到");
      pClient->disconnect();
      return false;
    }

    pChr = pSvc->getCharacteristic(CHAR_UUID);
    if (!pChr || !pChr->canNotify()) {
      DEBUG_PRINTLN("特征值未找到");
      pClient->disconnect();
      return false;
    }

    if (!pChr->subscribe(true, notifyCallback)) {
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
      pChr->writeValue(frame, 20);
    }
  }

  void handleNotification(uint8_t* pData, size_t length) {
    lastNotifyTime = millis();

    for (size_t i = 0; i < length; i++) {
      if (!rxActive && pData[i] == 0x55) {
        if (i + 3 < length && pData[i+1] == 0xAA && pData[i+2] == 0xEB && pData[i+3] == 0x90) {
          rxActive = true;
          rxIndex = 0;
          for (int j = 0; j < 4 && i < length; j++, i++) {
            rxBuffer[rxIndex++] = pData[i];
          }
          i--;
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
    // 单体电压 (6-69字节, 最多32串, 每串2字节, 单位0.001V)
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

    // 电池电压 (118-121字节, uint32, 0.001V)
    bmsData.batteryVoltage = (rxBuffer[118] | (rxBuffer[119] << 8) |
                              (rxBuffer[120] << 16) | (rxBuffer[121] << 24)) * 0.001;

    // 电池功率 (122-125字节, uint32, 0.001W)
    bmsData.batteryPower = (rxBuffer[122] | (rxBuffer[123] << 8) |
                            (rxBuffer[124] << 16) | (rxBuffer[125] << 24)) * 0.001;

    // 充电电流 (126-129字节, int32, 0.001A) 正值充电，负值放电
    int32_t currentRaw = rxBuffer[126] | (rxBuffer[127] << 8) |
                         (rxBuffer[128] << 16) | (rxBuffer[129] << 24);
    bmsData.chargeCurrent = currentRaw * 0.001;

    // MOS温度 (112-113字节, int16, 0.1C)
    bmsData.tempMOS = (int16_t)(rxBuffer[112] | (rxBuffer[113] << 8)) * 0.1;

    // 温度传感器1 (130-131字节, int16, 0.1C)
    bmsData.tempT1 = (int16_t)(rxBuffer[130] | (rxBuffer[131] << 8)) * 0.1;

    // 温度传感器2 (132-133字节, int16, 0.1C)
    bmsData.tempT2 = (int16_t)(rxBuffer[132] | (rxBuffer[133] << 8)) * 0.1;

    // 错误码 (134-135字节)
    uint16_t errors = rxBuffer[134] | (rxBuffer[135] << 8);

    // 均衡电流 (138-139字节, int16, 0.001A)
    bmsData.balancing = (rxBuffer[140] != 0);

    // SOC (141字节)
    bmsData.soc = rxBuffer[141];

    // 剩余容量 (142-145字节, uint32, 0.001Ah)
    bmsData.capacityRemain = (rxBuffer[142] | (rxBuffer[143] << 8) |
                              (rxBuffer[144] << 16) | (rxBuffer[145] << 24)) * 0.001;

    // 标称容量 (146-149字节, uint32, 0.001Ah)
    bmsData.nominalCapacity = (rxBuffer[146] | (rxBuffer[147] << 8) |
                               (rxBuffer[148] << 16) | (rxBuffer[149] << 24)) * 0.001;

    // 循环次数 (150-153字节)
    bmsData.cycleCount = rxBuffer[150] | (rxBuffer[151] << 8) |
                         (rxBuffer[152] << 16) | (rxBuffer[153] << 24);

    // 循环容量 (154-157字节, uint32, 0.001Ah)
    bmsData.cycleCapacity = (rxBuffer[154] | (rxBuffer[155] << 8) |
                             (rxBuffer[156] << 16) | (rxBuffer[157] << 24)) * 0.001;

    // 充放电状态
    bmsData.charging = (bmsData.chargeCurrent > 0.1);
    bmsData.discharging = (bmsData.chargeCurrent < -0.1);
    bmsData.valid = true;
    bmsData.lastUpdate = millis();

    DEBUG_PRINTF("电压:%.2fV 电流:%.2fA 功率:%.1fW SOC:%d%% 温度:%.1fC\n",
                 bmsData.batteryVoltage, bmsData.chargeCurrent,
                 bmsData.batteryPower, bmsData.soc, bmsData.tempT1);
  }

  void parseDeviceInfo() {
    // 设备信息帧解析 (如需要可扩展)
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
    // 配置背光引脚 (IO21，高电平点亮)
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(MC_DATUM);

    // 绘制静态框架
    drawFrame();
  }

  void update() {
    if (!bmsData.valid) {
      drawConnecting();
      return;
    }

    // 功率 (主视觉 - 顶部大字)
    drawPower();

    // 容量信息 (中部)
    drawCapacity();

    // 电压电流 (中下部)
    drawVoltageCurrent();

    // 温度 (底部)
    drawTemperature();

    // 状态指示器
    drawStatus();
  }

private:
  unsigned long lastUpdate = 0;
  float lastPower = -999;
  int lastSOC = -1;
  float lastVoltage = -1;
  float lastCurrent = -999;
  float lastTemp = -999;

  void drawFrame() {
    tft.fillScreen(COLOR_BG);

    // 顶部标题栏
    tft.fillRect(0, 0, SCREEN_WIDTH, 28, COLOR_DIM);
    tft.drawFastHLine(0, 28, SCREEN_WIDTH, COLOR_PRIMARY);

    tft.setTextColor(COLOR_TEXT, COLOR_DIM);
    tft.setTextSize(1);
    tft.setFreeFont(nullptr);
    tft.drawString("JK-BMS MONITOR", SCREEN_WIDTH / 2, 14);

    // 分隔线
    tft.drawFastHLine(10, 155, SCREEN_WIDTH - 20, COLOR_GRID);
    tft.drawFastHLine(10, 235, SCREEN_WIDTH - 20, COLOR_GRID);

    // 底部状态栏背景
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

    // 只重绘变化的部分
    if (abs(power - lastPower) < 1 && lastSOC == bmsData.soc) return;
    lastPower = power;
    lastSOC = bmsData.soc;

    // 功率区域背景 (32-150)
    tft.fillRect(0, 32, SCREEN_WIDTH, 120, COLOR_BG);

    // 功率标签
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString(isDischarge ? "DISCHARGE POWER" : "CHARGE POWER", SCREEN_WIDTH / 2, 44);

    // 功率数值 (超大字)
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

    // 功率进度条 (动态条)
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
      // 渐变效果
      for (int i = 0; i < fillWidth - 4; i++) {
        uint16_t c = barColor;
        if (i > fillWidth * 0.7) c = COLOR_WARNING;
        tft.drawFastVLine(barX + 2 + i, barY + 2, barHeight - 4, c);
      }
    }

    // SOC环形指示 (右上角小圆)
    drawSOCRing(190, 70, 22, bmsData.soc);
  }

  void drawSOCRing(int cx, int cy, int r, int soc) {
    // 背景圆环
    for (int i = 0; i < 360; i += 6) {
      float rad = i * PI / 180;
      int x1 = cx + (r - 3) * cos(rad);
      int y1 = cy + (r - 3) * sin(rad);
      int x2 = cx + r * cos(rad);
      int y2 = cy + r * sin(rad);
      tft.drawLine(x1, y1, x2, y2, COLOR_DIM);
    }

    // 进度弧
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

    // SOC数值
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

    // 区域 (160-230)
    tft.fillRect(0, 160, SCREEN_WIDTH, 72, COLOR_BG);

    // 容量标签
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("BATTERY CAPACITY", SCREEN_WIDTH / 2, 168);

    // 容量数值
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

    // 容量进度条
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

    // 区域 (240-290)
    tft.fillRect(0, 240, SCREEN_WIDTH, 52, COLOR_BG);

    // 左 - 电压
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.drawString("VOLTAGE", 60, 248);

    char vStr[16];
    sprintf(vStr, "%.2fV", voltage);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString(vStr, 60, 268);

    // 右 - 电流
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

    // 底部状态栏 (295-320)
    tft.fillRect(0, 296, SCREEN_WIDTH, 24, COLOR_DIM);

    // 温度
    char tempStr[32];
    sprintf(tempStr, "T:%.0fC", maxTemp);

    uint16_t tColor = maxTemp > 55 ? COLOR_ACCENT : (maxTemp > 45 ? COLOR_WARNING : COLOR_PRIMARY);
    tft.setTextColor(tColor, COLOR_DIM);
    tft.setTextSize(1);
    tft.drawString(tempStr, 45, 308);

    // 串数
    char cellStr[16];
    sprintf(cellStr, "%dS", bmsData.cellCount);
    tft.setTextColor(COLOR_TEXT, COLOR_DIM);
    tft.drawString(cellStr, 120, 308);

    // 循环次数
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

    // 状态指示灯 (标题栏右侧)
    int y = 14;
    tft.fillCircle(185, y, 4, bmsData.charging ? COLOR_PRIMARY : COLOR_DIM);
    tft.fillCircle(200, y, 4, bmsData.discharging ? COLOR_ACCENT : COLOR_DIM);
    tft.fillCircle(215, y, 4, bmsData.balancing ? COLOR_WARNING : COLOR_DIM);
  }

  float lastCapacity = -1;
  float lastTotalCapacity = -1;
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

  // 初始化显示屏
  dashboard.begin();

  // 启动画面
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.setTextSize(2);
  tft.drawString("JKBMS", SCREEN_WIDTH / 2, 120);
  tft.setTextColor(COLOR_SECONDARY, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("Initializing BLE...", SCREEN_WIDTH / 2, 150);

  // 初始化BLE
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
// JKBMS_BLE 静态回调实现
// ============================================================
void JKBMS_BLE::notifyCallback(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
  jkbms.handleNotification(pData, length);
}

void JKBMS_BLE::ScanCallbacks::onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
  std::string addr = advertisedDevice->getAddress().toString();
  if (addr == jkbms.targetMAC) {
    DEBUG_PRINTF("找到BMS: %s\n", addr.c_str());
    jkbms.advDevice = advertisedDevice;
    jkbms.doConnect = true;
    NimBLEDevice::getScan()->stop();
  }
}

// ============================================================
// 主循环
// ============================================================
void loop() {
  // BLE通信处理
  jkbms.loop();

  // 显示刷新
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > DISPLAY_REFRESH_MS) {
    dashboard.update();
    lastDisplayUpdate = millis();
  }

  delay(10);
}
