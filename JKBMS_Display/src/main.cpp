/*
  JKBMS Bluetooth Monitor for ESP32-32E + ST7789 2.8" Display

  功能：通过蓝牙连接JKBMS，解析电池数据并显示到ST7789屏幕
  硬件：ESP32-32E + 2.8寸 ST7789P3 显示屏 (240x320)
  BMS: JK_BD4A24S10P

  库依赖：
  - NimBLE-Arduino (by h2zero)
  - TFT_eSPI (by Bodmer)

  接线说明 (ESP32-32E 官方引脚分配):
  - CS   -> GPIO15  (TFT_CS)
  - DC   -> GPIO2   (TFT_RS)
  - RST  -> EN      (与开发板复位共享)
  - MOSI -> GPIO13  (TFT_MOSI)
  - SCK  -> GPIO14  (TFT_SCK)
  - MISO -> GPIO12  (TFT_MISO)
  - BL   -> GPIO21  (TFT_BL, 背光控制)
  - VCC  -> 3.3V
  - GND  -> GND
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

// ===================== 用户配置区域 =====================
// BMS蓝牙配置
#define BMS_MAC_ADDRESS "98:DA:20:07:B9:00"
#define BMS_NAME "JK_BD4A24S10P"

// 调试开关
#define DEBUG_ENABLED true

// 显示刷新间隔 (毫秒)
#define DISPLAY_UPDATE_INTERVAL 500
#define BMS_POLL_INTERVAL 2000

// ===================== 调试宏 =====================
#if DEBUG_ENABLED
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
  #define DEBUG_PRINTF(...)
#endif

// ===================== BLE UUID =====================
#define SERVICE_UUID "ffe0"
#define CHARACTERISTIC_UUID "ffe1"

// ===================== 命令定义 =====================
#define CMD_DEVICE_INFO 0x97
#define CMD_CELL_INFO 0x96

// ===================== 数据帧类型 =====================
#define FRAME_SETTINGS 0x01
#define FRAME_CELL_INFO 0x02
#define FRAME_DEVICE_INFO 0x03

// ===================== 颜色定义 (电竞风格) =====================
#define COLOR_BG TFT_BLACK
#define COLOR_PRIMARY 0x07FF    // 青色
#define COLOR_SECONDARY 0xF81F  // 紫色
#define COLOR_ACCENT 0xFFE0     // 黄色
#define COLOR_DANGER 0xF800     // 红色
#define COLOR_SUCCESS 0x07E0    // 绿色
#define COLOR_WHITE TFT_WHITE
#define COLOR_GRAY 0x8410       // 深灰
#define COLOR_DARK_GRAY 0x4208  // 更深灰
#define COLOR_CHARGE 0x07E0     // 充电绿
#define COLOR_DISCHARGE 0xF800  // 放电红
#define COLOR_POWER_BG 0x1082   // 功率背景

// ===================== 全局对象 =====================
TFT_eSPI tft = TFT_eSPI();

// ===================== BMS数据结构 =====================
struct BMSData {
  // 核心数据
  float batteryVoltage = 0;
  float batteryCurrent = 0;
  float batteryPower = 0;
  int soc = 0;
  float capacityRemain = 0;
  float nominalCapacity = 0;
  float cycleCount = 0;
  
  // 温度
  float tempT1 = 0;
  float tempT2 = 0;
  float tempMOS = 0;
  float tempT3 = 0;
  float tempT4 = 0;
  float tempT5 = 0;
  
  // 状态
  bool chargeMOS = false;
  bool dischargeMOS = false;
  bool balancing = false;
  int balancingAction = 0;
  
  // 单体数据
  float cellVoltages[24] = {0};
  float cellResistances[24] = {0};
  int cellCount = 0;
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  float maxCellVoltage = 0;
  float minCellVoltage = 0;
  
  // 错误
  uint16_t errorBitmask = 0;
  
  // 连接状态
  bool connected = false;
  unsigned long lastUpdate = 0;
};

BMSData bmsData;

// ===================== BLE相关 =====================
static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static NimBLEAdvertisedDevice* myDevice = nullptr;

bool doConnect = false;
bool connected = false;
bool newDataAvailable = false;

// 数据接收缓冲区
uint8_t rxBuffer[320];
int rxIndex = 0;
bool rxStarted = false;
bool frameComplete = false;

// ===================== 时间控制 =====================
unsigned long lastDisplayUpdate = 0;
unsigned long lastBmsPoll = 0;
unsigned long lastNotifyTime = 0;

// ===================== 字体和布局 =====================
// 2.8寸 240x320 竖屏布局
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// 布局区域定义
#define AREA_POWER_Y 0
#define AREA_POWER_H 110
#define AREA_CAPACITY_Y 110
#define AREA_CAPACITY_H 90
#define AREA_TEMP_Y 200
#define AREA_TEMP_H 60
#define AREA_STATUS_Y 260
#define AREA_STATUS_H 60

// ===================== 类定义 =====================
class MyClientCallback : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) {
    connected = true;
    bmsData.connected = true;
    DEBUG_PRINTLN("BLE Connected");
  }

  void onDisconnect(NimBLEClient* pclient) {
    connected = false;
    bmsData.connected = false;
    doConnect = false;
    DEBUG_PRINTLN("BLE Disconnected");
  }
};

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    DEBUG_PRINTF("Found device: %s\n", advertisedDevice->toString().c_str());
    
    if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(BLEUUID(SERVICE_UUID))) {
      DEBUG_PRINTLN("Found JKBMS service!");
      
      if (advertisedDevice->getAddress().toString() == BMS_MAC_ADDRESS) {
        DEBUG_PRINTLN("MAC address matched!");
        BLEDevice::getScan()->stop();
        myDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        doConnect = true;
      }
    }
  }
};

// ===================== CRC计算 =====================
uint8_t calculateCRC(const uint8_t data[], uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) {
    crc += data[i];
  }
  return crc;
}

// ===================== 发送命令 =====================
void sendCommand(uint8_t cmd) {
  if (!pRemoteCharacteristic) return;
  
  uint8_t frame[20] = {0};
  frame[0] = 0xAA; frame[1] = 0x55; frame[2] = 0x90; frame[3] = 0xEB;
  frame[4] = cmd;
  frame[5] = 0x00;
  frame[6] = 0x00; frame[7] = 0x00; frame[8] = 0x00; frame[9] = 0x00;
  
  frame[19] = calculateCRC(frame, 19);
  
  DEBUG_PRINTF("Sending cmd 0x%02X\n", cmd);
  pRemoteCharacteristic->writeValue(frame, 20);
}

// ===================== 通知回调 =====================
static void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic,
                          uint8_t* pData, size_t length, bool isNotify) {
  
  for (size_t i = 0; i < length; i++) {
    // 检测帧头
    if (!rxStarted && i < length - 3 && 
        pData[i] == 0x55 && pData[i+1] == 0xAA && 
        pData[i+2] == 0xEB && pData[i+3] == 0x90) {
      rxStarted = true;
      rxIndex = 0;
      // 复制帧头
      for (int j = 0; j < 4 && (i + j) < length; j++) {
        rxBuffer[rxIndex++] = pData[i + j];
      }
      i += 3;
      continue;
    }
    
    if (rxStarted && rxIndex < 320) {
      rxBuffer[rxIndex++] = pData[i];
      
      if (rxIndex >= 300) {
        frameComplete = true;
        rxStarted = false;
        break;
      }
    }
  }
  
  lastNotifyTime = millis();
}

// ===================== 解析数据 =====================
void parseCellInfo() {
  if (rxIndex < 150) return;
  
  // CRC校验
  uint8_t crc = calculateCRC(rxBuffer, rxIndex - 1);
  if (crc != rxBuffer[rxIndex - 1]) {
    DEBUG_PRINTF("CRC error: calc=0x%02X, recv=0x%02X\n", crc, rxBuffer[rxIndex - 1]);
    return;
  }
  
  DEBUG_PRINTLN("Parsing cell info...");
  
  // 解析单体电压 (最多24串)
  bmsData.cellCount = 0;
  bmsData.maxCellVoltage = 0;
  bmsData.minCellVoltage = 999;
  float sumVoltage = 0;
  int validCells = 0;
  
  for (int i = 0; i < 24; i++) {
    int offset = 6 + i * 2;
    if (offset + 1 < rxIndex) {
      uint16_t raw = rxBuffer[offset] | (rxBuffer[offset + 1] << 8);
      bmsData.cellVoltages[i] = raw * 0.001;
      if (raw > 0) {
        bmsData.cellCount = i + 1;
        sumVoltage += bmsData.cellVoltages[i];
        validCells++;
        if (bmsData.cellVoltages[i] > bmsData.maxCellVoltage) 
          bmsData.maxCellVoltage = bmsData.cellVoltages[i];
        if (bmsData.cellVoltages[i] < bmsData.minCellVoltage) 
          bmsData.minCellVoltage = bmsData.cellVoltages[i];
      }
    }
  }
  
  if (validCells > 0) {
    bmsData.avgCellVoltage = sumVoltage / validCells;
    bmsData.deltaCellVoltage = bmsData.maxCellVoltage - bmsData.minCellVoltage;
  }
  
  // 解析MOS温度 (偏移112-113)
  if (rxIndex > 113) {
    int16_t raw = rxBuffer[112] | (rxBuffer[113] << 8);
    bmsData.tempMOS = raw * 0.1;
  }
  
  // 解析电池电压 (偏移118-121)
  if (rxIndex > 121) {
    uint32_t raw = rxBuffer[118] | (rxBuffer[119] << 8) | 
                   (rxBuffer[120] << 16) | (rxBuffer[121] << 24);
    bmsData.batteryVoltage = raw * 0.001;
  }
  
  // 解析电池功率 (偏移122-125)
  if (rxIndex > 125) {
    uint32_t raw = rxBuffer[122] | (rxBuffer[123] << 8) | 
                   (rxBuffer[124] << 16) | (rxBuffer[125] << 24);
    bmsData.batteryPower = raw * 0.001;
  }
  
  // 解析电流 (偏移126-129)
  if (rxIndex > 129) {
    int32_t raw = rxBuffer[126] | (rxBuffer[127] << 8) | 
                  (rxBuffer[128] << 16) | (rxBuffer[129] << 24);
    bmsData.batteryCurrent = raw * 0.001;
  }
  
  // 解析温度1 (偏移130-131)
  if (rxIndex > 131) {
    int16_t raw = rxBuffer[130] | (rxBuffer[131] << 8);
    bmsData.tempT1 = raw * 0.1;
  }
  
  // 解析温度2 (偏移132-133)
  if (rxIndex > 133) {
    int16_t raw = rxBuffer[132] | (rxBuffer[133] << 8);
    bmsData.tempT2 = raw * 0.1;
  }
  
  // 解析错误码 (偏移134-135)
  if (rxIndex > 135) {
    bmsData.errorBitmask = rxBuffer[134] | (rxBuffer[135] << 8);
  }
  
  // 解析SOC (偏移141)
  if (rxIndex > 141) {
    bmsData.soc = rxBuffer[141];
  }
  
  // 解析剩余容量 (偏移142-145)
  if (rxIndex > 145) {
    uint32_t raw = rxBuffer[142] | (rxBuffer[143] << 8) | 
                   (rxBuffer[144] << 16) | (rxBuffer[145] << 24);
    bmsData.capacityRemain = raw * 0.001;
  }
  
  // 解析标称容量 (偏移146-149)
  if (rxIndex > 149) {
    uint32_t raw = rxBuffer[146] | (rxBuffer[147] << 8) | 
                   (rxBuffer[148] << 16) | (rxBuffer[149] << 24);
    bmsData.nominalCapacity = raw * 0.001;
  }
  
  // 解析循环次数 (偏移150-153)
  if (rxIndex > 153) {
    uint32_t raw = rxBuffer[150] | (rxBuffer[151] << 8) | 
                   (rxBuffer[152] << 16) | (rxBuffer[153] << 24);
    bmsData.cycleCount = raw;
  }
  
  // 解析MOS状态 (偏移166-167)
  if (rxIndex > 167) {
    bmsData.chargeMOS = rxBuffer[166] != 0;
    bmsData.dischargeMOS = rxBuffer[167] != 0;
  }
  
  // 解析均衡状态 (偏移140)
  if (rxIndex > 140) {
    bmsData.balancingAction = rxBuffer[140];
    bmsData.balancing = (rxBuffer[140] != 0);
  }
  
  // 解析其他温度传感器
  if (rxIndex > 227) {
    int16_t raw3 = rxBuffer[226] | (rxBuffer[227] << 8);
    bmsData.tempT3 = raw3 * 0.1;
  }
  if (rxIndex > 225) {
    int16_t raw4 = rxBuffer[224] | (rxBuffer[225] << 8);
    bmsData.tempT4 = raw4 * 0.1;
  }
  if (rxIndex > 223) {
    int16_t raw5 = rxBuffer[222] | (rxBuffer[223] << 8);
    bmsData.tempT5 = raw5 * 0.1;
  }
  
  bmsData.lastUpdate = millis();
  newDataAvailable = true;
  
  DEBUG_PRINTF("Voltage: %.2fV, Current: %.2fA, Power: %.2fW, SOC: %d%%\n",
               bmsData.batteryVoltage, bmsData.batteryCurrent, 
               bmsData.batteryPower, bmsData.soc);
}

void parseDeviceInfo() {
  DEBUG_PRINTLN("Parsing device info...");
  // 设备信息解析，这里简化处理
}

void processFrame() {
  if (!frameComplete) return;
  frameComplete = false;
  
  uint8_t frameType = rxBuffer[4];
  
  switch (frameType) {
    case FRAME_CELL_INFO:
      parseCellInfo();
      break;
    case FRAME_DEVICE_INFO:
      parseDeviceInfo();
      break;
    case FRAME_SETTINGS:
      DEBUG_PRINTLN("Settings frame received");
      break;
    default:
      DEBUG_PRINTF("Unknown frame type: 0x%02X\n", frameType);
  }
}

// ===================== BLE连接 =====================
bool connectToServer() {
  DEBUG_PRINT("Connecting to BMS...");
  
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  
  if (!pClient->connect(myDevice)) {
    DEBUG_PRINTLN(" FAILED!");
    return false;
  }
  
  DEBUG_PRINTLN(" Connected!");
  
  NimBLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (!pRemoteService) {
    DEBUG_PRINTLN("Service not found!");
    pClient->disconnect();
    return false;
  }
  
  pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
  if (!pRemoteCharacteristic) {
    DEBUG_PRINTLN("Characteristic not found!");
    pClient->disconnect();
    return false;
  }
  
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->subscribe(true, notifyCallback);
    DEBUG_PRINTLN("Notifications registered");
  }
  
  delay(500);
  sendCommand(CMD_DEVICE_INFO);
  delay(500);
  sendCommand(CMD_CELL_INFO);
  
  return true;
}

// ===================== UI绘制函数 =====================

// 绘制圆角矩形进度条
void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color, uint16_t bgColor) {
  int fillW = (int)(w * percent / 100.0);
  if (fillW > w) fillW = w;
  
  // 背景
  tft.fillRoundRect(x, y, w, h, h/2, bgColor);
  // 填充
  if (fillW > 0) {
    tft.fillRoundRect(x, y, fillW, h, h/2, color);
  }
  // 边框
  tft.drawRoundRect(x, y, w, h, h/2, COLOR_GRAY);
}

// 绘制带发光效果的文字
void drawGlowText(int x, int y, const char* text, uint16_t color, int size) {
  tft.setTextSize(size);
  
  // 发光效果 (多层绘制)
  tft.setTextColor(tft.color565(
    (color >> 11) & 0x1F ? 8 : 0,
    (color >> 5) & 0x3F ? 16 : 0,
    color & 0x1F ? 8 : 0
  ), COLOR_BG);
  
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx != 0 || dy != 0) {
        tft.drawString(text, x + dx, y + dy);
      }
    }
  }
  
  // 主文字
  tft.setTextColor(color, COLOR_BG);
  tft.drawString(text, x, y);
}

// 绘制功率区域 (主视觉)
void drawPowerArea() {
  int y = AREA_POWER_Y;
  int h = AREA_POWER_H;
  
  // 背景渐变效果 (用色块模拟)
  tft.fillRect(0, y, SCREEN_WIDTH, h, COLOR_BG);
  
  // 顶部装饰线
  tft.drawFastHLine(10, y + 2, SCREEN_WIDTH - 20, COLOR_PRIMARY);
  
  // 功率标签
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("POWER", 15, y + 8);
  
  // 充放电状态指示
  const char* statusText = "";
  uint16_t statusColor = COLOR_GRAY;
  
  if (bmsData.batteryCurrent > 0.5) {
    statusText = "CHARGING";
    statusColor = COLOR_CHARGE;
  } else if (bmsData.batteryCurrent < -0.5) {
    statusText = "DISCHARGING";
    statusColor = COLOR_DISCHARGE;
  } else {
    statusText = "STANDBY";
    statusColor = COLOR_GRAY;
  }
  
  tft.setTextColor(statusColor, COLOR_BG);
  tft.drawString(statusText, SCREEN_WIDTH - 80, y + 8);
  
  // 功率大数字
  char powerStr[16];
  float power = abs(bmsData.batteryPower);
  if (power >= 1000) {
    snprintf(powerStr, sizeof(powerStr), "%.2f", power / 1000.0);
  } else {
    snprintf(powerStr, sizeof(powerStr), "%.0f", power);
  }
  
  tft.setTextSize(3);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  int textW = tft.textWidth(powerStr);
  tft.drawString(powerStr, 15, y + 25);
  
  // 单位
  tft.setTextSize(2);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  if (power >= 1000) {
    tft.drawString("kW", 15 + textW + 5, y + 32);
  } else {
    tft.drawString("W", 15 + textW + 5, y + 32);
  }
  
  // 电流和电压 (次要信息)
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  
  char infoStr[32];
  snprintf(infoStr, sizeof(infoStr), "%.2fV  %.2fA", 
           bmsData.batteryVoltage, bmsData.batteryCurrent);
  tft.drawString(infoStr, 15, y + 60);
  
  // 功率进度条 (动态效果)
  float powerPercent = 0;
  if (bmsData.batteryPower > 0) {
    powerPercent = min(bmsData.batteryPower / 5000.0 * 100.0, 100.0);
  }
  
  uint16_t barColor = COLOR_PRIMARY;
  if (bmsData.batteryPower > 3000) barColor = COLOR_ACCENT;
  if (bmsData.batteryPower > 4500) barColor = COLOR_DANGER;
  
  drawProgressBar(15, y + 78, SCREEN_WIDTH - 30, 10, powerPercent, barColor, COLOR_DARK_GRAY);
  
  // SOC百分比小标签
  char socStr[8];
  snprintf(socStr, sizeof(socStr), "%d%%", bmsData.soc);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.drawString(socStr, SCREEN_WIDTH - 35, y + 60);
  
  // 分隔线
  tft.drawFastHLine(10, y + h - 1, SCREEN_WIDTH - 20, COLOR_DARK_GRAY);
}

// 绘制容量区域
void drawCapacityArea() {
  int y = AREA_CAPACITY_Y;
  int h = AREA_CAPACITY_H;
  
  tft.fillRect(0, y, SCREEN_WIDTH, h, COLOR_BG);
  
  // 标签
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("CAPACITY", 15, y + 5);
  
  // 容量大数字
  char capStr[16];
  snprintf(capStr, sizeof(capStr), "%.1f", bmsData.capacityRemain);
  
  tft.setTextSize(2);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.drawString(capStr, 15, y + 22);
  
  tft.setTextSize(1);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.drawString("Ah", tft.textWidth(capStr) * 2 + 20, y + 28);
  
  // 分隔符 /
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("/", 100, y + 28);
  
  // 总容量
  char totalStr[16];
  snprintf(totalStr, sizeof(totalStr), "%.1f", bmsData.nominalCapacity);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString(totalStr, 115, y + 28);
  tft.drawString("Ah", tft.textWidth(totalStr) * 2 + 120, y + 28);
  
  // SOC大进度条
  drawProgressBar(15, y + 52, SCREEN_WIDTH - 30, 18, bmsData.soc, COLOR_SUCCESS, COLOR_DARK_GRAY);
  
  // SOC数字在进度条中间
  char socStr[8];
  snprintf(socStr, sizeof(socStr), "%d%%", bmsData.soc);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  int socW = tft.textWidth(socStr);
  tft.drawString(socStr, (SCREEN_WIDTH - socW) / 2, y + 55);
  
  // 循环次数
  char cycleStr[16];
  snprintf(cycleStr, sizeof(cycleStr), "Cycles: %.0f", bmsData.cycleCount);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString(cycleStr, SCREEN_WIDTH - 80, y + 5);
  
  // 分隔线
  tft.drawFastHLine(10, y + h - 1, SCREEN_WIDTH - 20, COLOR_DARK_GRAY);
}

// 绘制温度区域
void drawTempArea() {
  int y = AREA_TEMP_Y;
  int h = AREA_TEMP_H;
  
  tft.fillRect(0, y, SCREEN_WIDTH, h, COLOR_BG);
  
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("TEMPERATURE", 15, y + 5);
  
  // 温度显示
  char tempStr[32];
  
  // 主温度 (T1)
  uint16_t t1Color = COLOR_PRIMARY;
  if (bmsData.tempT1 > 45) t1Color = COLOR_ACCENT;
  if (bmsData.tempT1 > 55) t1Color = COLOR_DANGER;
  
  snprintf(tempStr, sizeof(tempStr), "T1:%.1f", bmsData.tempT1);
  tft.setTextColor(t1Color, COLOR_BG);
  tft.drawString(tempStr, 15, y + 22);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("C", tft.textWidth(tempStr) * 1 + 18, y + 22);
  
  // T2
  uint16_t t2Color = COLOR_PRIMARY;
  if (bmsData.tempT2 > 45) t2Color = COLOR_ACCENT;
  if (bmsData.tempT2 > 55) t2Color = COLOR_DANGER;
  
  snprintf(tempStr, sizeof(tempStr), "T2:%.1f", bmsData.tempT2);
  tft.setTextColor(t2Color, COLOR_BG);
  tft.drawString(tempStr, 90, y + 22);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("C", 90 + tft.textWidth(tempStr) * 1 + 3, y + 22);
  
  // MOS温度
  uint16_t mosColor = COLOR_PRIMARY;
  if (bmsData.tempMOS > 50) mosColor = COLOR_ACCENT;
  if (bmsData.tempMOS > 70) mosColor = COLOR_DANGER;
  
  snprintf(tempStr, sizeof(tempStr), "MOS:%.1f", bmsData.tempMOS);
  tft.setTextColor(mosColor, COLOR_BG);
  tft.drawString(tempStr, 165, y + 22);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("C", 165 + tft.textWidth(tempStr) * 1 + 3, y + 22);
  
  // 温度条 (T1)
  float tempPercent = min(max((bmsData.tempT1 + 20) / 80.0 * 100.0, 0.0), 100.0);
  uint16_t tempBarColor = COLOR_SUCCESS;
  if (bmsData.tempT1 > 35) tempBarColor = COLOR_ACCENT;
  if (bmsData.tempT1 > 50) tempBarColor = COLOR_DANGER;
  
  drawProgressBar(15, y + 42, 100, 6, tempPercent, tempBarColor, COLOR_DARK_GRAY);
  
  // 分隔线
  tft.drawFastHLine(10, y + h - 1, SCREEN_WIDTH - 20, COLOR_DARK_GRAY);
}

// 绘制状态区域
void drawStatusArea() {
  int y = AREA_STATUS_Y;
  int h = AREA_STATUS_H;
  
  tft.fillRect(0, y, SCREEN_WIDTH, h, COLOR_BG);
  
  // 连接状态
  if (bmsData.connected) {
    tft.fillCircle(20, y + 10, 4, COLOR_SUCCESS);
    tft.setTextColor(COLOR_SUCCESS, COLOR_BG);
    tft.drawString("ONLINE", 30, y + 6);
  } else {
    tft.fillCircle(20, y + 10, 4, COLOR_DANGER);
    tft.setTextColor(COLOR_DANGER, COLOR_BG);
    tft.drawString("OFFLINE", 30, y + 6);
  }
  
  // MOS状态指示
  int x = 100;
  
  // 充电MOS
  if (bmsData.chargeMOS) {
    tft.fillRoundRect(x, y + 5, 28, 14, 3, COLOR_CHARGE);
    tft.setTextColor(COLOR_BG, COLOR_CHARGE);
  } else {
    tft.drawRoundRect(x, y + 5, 28, 14, 3, COLOR_GRAY);
    tft.setTextColor(COLOR_GRAY, COLOR_BG);
  }
  tft.drawString("CHG", x + 3, y + 7);
  
  // 放电MOS
  x += 35;
  if (bmsData.dischargeMOS) {
    tft.fillRoundRect(x, y + 5, 28, 14, 3, COLOR_DISCHARGE);
    tft.setTextColor(COLOR_BG, COLOR_DISCHARGE);
  } else {
    tft.drawRoundRect(x, y + 5, 28, 14, 3, COLOR_GRAY);
    tft.setTextColor(COLOR_GRAY, COLOR_BG);
  }
  tft.drawString("DSG", x + 3, y + 7);
  
  // 均衡
  x += 35;
  if (bmsData.balancing) {
    tft.fillRoundRect(x, y + 5, 28, 14, 3, COLOR_ACCENT);
    tft.setTextColor(COLOR_BG, COLOR_ACCENT);
  } else {
    tft.drawRoundRect(x, y + 5, 28, 14, 3, COLOR_GRAY);
    tft.setTextColor(COLOR_GRAY, COLOR_BG);
  }
  tft.drawString("BAL", x + 3, y + 7);
  
  // 错误指示
  if (bmsData.errorBitmask != 0) {
    tft.fillRoundRect(SCREEN_WIDTH - 45, y + 5, 35, 14, 3, COLOR_DANGER);
    tft.setTextColor(COLOR_BG, COLOR_DANGER);
    tft.drawString("ERR", SCREEN_WIDTH - 40, y + 7);
  }
  
  // 单体信息 (底部)
  char cellStr[48];
  snprintf(cellStr, sizeof(cellStr), "%dS  %.3fV  d%.3fV", 
           bmsData.cellCount, bmsData.avgCellVoltage, bmsData.deltaCellVoltage);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString(cellStr, 15, y + 30);
  
  // 底部装饰线
  tft.drawFastHLine(10, SCREEN_HEIGHT - 2, SCREEN_WIDTH - 20, COLOR_PRIMARY);
}

// 绘制启动画面
void drawSplashScreen() {
  tft.fillScreen(COLOR_BG);
  
  // 边框
  tft.drawRect(10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20, COLOR_PRIMARY);
  tft.drawRect(12, 12, SCREEN_WIDTH - 24, SCREEN_HEIGHT - 24, COLOR_SECONDARY);
  
  // 标题
  tft.setTextSize(2);
  tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
  tft.drawString("JKBMS", 80, 80);
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.drawString("MONITOR", 70, 105);
  
  // 版本
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString("v1.0 ESP32 BLE", 80, 140);
  
  // BMS名称
  tft.setTextColor(COLOR_SECONDARY, COLOR_BG);
  tft.drawString(BMS_NAME, 65, 170);
  
  // MAC地址
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.drawString(BMS_MAC_ADDRESS, 55, 190);
  
  // 加载动画
  for (int i = 0; i < 3; i++) {
    tft.fillCircle(100 + i * 20, 230, 5, COLOR_PRIMARY);
    delay(200);
  }
  
  delay(500);
  tft.fillScreen(COLOR_BG);
}

// 绘制离线画面
void drawOfflineScreen() {
  static unsigned long lastBlink = 0;
  static bool blink = false;
  
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    blink = !blink;
    
    tft.fillScreen(COLOR_BG);
    
    tft.setTextSize(2);
    if (blink) {
      tft.setTextColor(COLOR_DANGER, COLOR_BG);
      tft.drawString("NO SIGNAL", 60, 130);
    } else {
      tft.setTextColor(COLOR_GRAY, COLOR_BG);
      tft.drawString("SEARCHING...", 50, 130);
    }
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_GRAY, COLOR_BG);
    tft.drawString(BMS_MAC_ADDRESS, 55, 160);
    tft.drawString(BMS_NAME, 80, 175);
    
    // 重连提示
    tft.setTextColor(COLOR_PRIMARY, COLOR_BG);
    tft.drawString("Retrying BLE connection...", 45, 210);
  }
}

// 主显示更新
void updateDisplay() {
  if (!bmsData.connected && millis() - lastNotifyTime > 5000) {
    drawOfflineScreen();
    return;
  }
  
  if (!newDataAvailable) return;
  newDataAvailable = false;
  
  drawPowerArea();
  drawCapacityArea();
  drawTempArea();
  drawStatusArea();
}

// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("\n\nJKBMS Monitor Starting...");
  
  // 初始化显示屏
  tft.init();
  tft.setRotation(0);  // 竖屏
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  
  drawSplashScreen();
  
  // 初始化BLE
  DEBUG_PRINTLN("Init BLE...");
  BLEDevice::init("JKBMS-Monitor");
  
  NimBLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30, false);
  
  DEBUG_PRINTLN("Setup complete");
}

// ===================== 主循环 =====================
void loop() {
  // 处理BLE连接
  if (doConnect) {
    if (connectToServer()) {
      DEBUG_PRINTLN("Connected to BMS");
      doConnect = false;
    } else {
      DEBUG_PRINTLN("Connection failed, retrying...");
      doConnect = false;
      delay(2000);
      BLEDevice::getScan()->start(10, false);
    }
  }
  
  // 如果断开连接，重新扫描
  if (!connected && !doConnect && millis() - lastNotifyTime > 10000) {
    DEBUG_PRINTLN("Reconnecting...");
    BLEDevice::getScan()->start(10, false);
    lastNotifyTime = millis();
  }
  
  // 处理接收到的数据帧
  processFrame();
  
  // 定期请求BMS数据
  if (connected && millis() - lastBmsPoll > BMS_POLL_INTERVAL) {
    sendCommand(CMD_CELL_INFO);
    lastBmsPoll = millis();
  }
  
  // 更新显示
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(10);
}
