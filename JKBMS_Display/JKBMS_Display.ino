/*
  JKBMS Bluetooth Monitor for ESP32-32E + ST7789 2.8" Display
  战斗风格UI - 汽车运动模式主题
  
  硬件:
  - ESP32-32E
  - 2.8寸 ST7789P3 显示屏 (320x240)
  
  连接方式:
  - ST7789 CS   -> GPIO 5
  - ST7789 DC   -> GPIO 16
  - ST7789 RST  -> GPIO 17
  - ST7789 MOSI -> GPIO 23
  - ST7789 SCK  -> GPIO 18
  - ST7789 LED  -> 3.3V (或PWM调光)
  
  库依赖:
  - TFT_eSPI (by Bodmer)
  - NimBLE-Arduino (by h2zero)
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>

// ==================== 配置区域 ====================
// BMS蓝牙配置
#define BMS_MAC_ADDRESS     "98:da:20:07:b9:00"
#define BMS_NAME            "JK_BD4A24S10P"

// BLE UUID
#define SERVICE_UUID        "ffe0"
#define CHARACTERISTIC_UUID "ffe1"

// 显示屏配置 (在TFT_eSPI的User_Setup.h中配置)
// 确保设置: ST7789_DRIVER, 320x240, SPI引脚正确

// ==================== 战斗风格颜色主题 ====================
// 基础颜色
#define COLOR_BG            0x0000    // 纯黑背景
#define COLOR_CARD_BG       0x18E3    // 深灰蓝卡片背景 #181A20
#define COLOR_CARD_BORDER   0x39E7    // 卡片边框 #333842

// 功率颜色渐变 (随功率变化)
#define COLOR_POWER_LOW     0x07E0    // 绿色 - 低功率
#define COLOR_POWER_MID     0xFFE0    // 黄色 - 中功率
#define COLOR_POWER_HIGH    0xFBE0    // 橙色 - 高功率
#define COLOR_POWER_MAX     0xF800    // 红色 - 最大功率

// 强调色
#define COLOR_ACCENT        0x05D9    // 霓虹青 #00FFCC
#define COLOR_ACCENT_DIM    0x02A8    // 暗青色
#define COLOR_TEXT_PRIMARY  0xFFFF    // 纯白
#define COLOR_TEXT_SECOND   0xBDF7    // 浅灰
#define COLOR_TEXT_DIM      0x7BEF    // 中灰

// 状态颜色
#define COLOR_BT_CONNECTED  0x07E0    // 蓝牙已连接 - 绿色
#define COLOR_BT_DISCONN    0xF800    // 蓝牙断开 - 红色
#define COLOR_CHARGE        0x07FF    // 充电中 - 青色
#define COLOR_DISCHARGE     0xFBE0    // 放电中 - 橙色

// ==================== 全局对象 ====================
TFT_eSPI tft = TFT_eSPI();

// BLE相关
static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static bool bleConnected = false;
static bool bleFound = false;

// 数据帧缓冲
uint8_t frameBuffer[320];
uint16_t frameIndex = 0;
bool frameStarted = false;
volatile bool newDataAvailable = false;

// BMS数据结构
struct BMSData {
  // 电压电流
  float batteryVoltage = 0;      // 总电压 V
  float chargeCurrent = 0;       // 电流 A (正=充电, 负=放电)
  float batteryPower = 0;        // 功率 W
  
  // 容量
  uint8_t soc = 0;               // 电量百分比
  float remainingCapacity = 0;   // 剩余容量 Ah
  float nominalCapacity = 0;     // 标称容量 Ah
  
  // 温度
  float mosTemp = 0;             // MOS管温度
  float tempSensor1 = 0;         // 温度传感器1
  float tempSensor2 = 0;         // 温度传感器2
  
  // 状态
  bool chargeMosOn = false;
  bool dischargeMosOn = false;
  bool balancing = false;
  uint16_t errors = 0;
  
  // 单体信息
  float cellVoltages[24] = {0};  // 最多24串
  uint8_t cellCount = 0;
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  
  // 其他
  uint32_t cycleCount = 0;
  float cycleCapacity = 0;
  uint8_t soh = 100;
} bmsData;

// UI状态
float displayPower = 0;          // 用于动画的显示功率
float targetPower = 0;
unsigned long lastUiUpdate = 0;
unsigned long lastBleActivity = 0;
uint8_t animationFrame = 0;

// 部分刷新优化
bool firstDraw = true;
float lastDisplayPower = -9999;
uint8_t lastSoc = 255;
bool lastBleState = false;

// ==================== 工具函数 ====================
uint8_t calculateCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) {
    crc += data[i];
  }
  return crc;
}

// 根据功率获取颜色
uint16_t getPowerColor(float power) {
  float absPower = abs(power);
  if (absPower < 100) return COLOR_POWER_LOW;
  if (absPower < 500) return COLOR_POWER_MID;
  if (absPower < 1000) return COLOR_POWER_HIGH;
  return COLOR_POWER_MAX;
}

// 绘制渐变效果的圆角卡片
void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, 
              uint16_t bgColor, uint16_t borderColor, bool glow = false) {
  // 绘制外发光效果
  if (glow) {
    tft.drawRoundRect(x-1, y-1, w+2, h+2, r+1, COLOR_ACCENT_DIM);
    tft.drawRoundRect(x-2, y-2, w+4, h+4, r+2, 0x0144);
  }
  // 填充背景
  tft.fillRoundRect(x, y, w, h, r, bgColor);
  // 绘制边框
  tft.drawRoundRect(x, y, w, h, r, borderColor);
  // 顶部高光线条
  tft.drawFastHLine(x+r, y, w-r*2, 0x4A69);
}

// ==================== 蓝牙回调 ====================
class BMSClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    Serial.println("蓝牙已连接!");
    bleConnected = true;
  }
  void onDisconnect(NimBLEClient* pClient, int reason) {
    Serial.println("蓝牙已断开!");
    bleConnected = false;
    bleFound = false;
  }
};

class BMSAdvertisedCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    if (advertisedDevice->getAddress().toString() == BMS_MAC_ADDRESS) {
      Serial.print("找到BMS设备: ");
      Serial.println(advertisedDevice->getName().c_str());
      bleFound = true;
      NimBLEDevice::getScan()->stop();
    }
  }
};

void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lastBleActivity = millis();
  
  for (size_t i = 0; i < length; i++) {
    // 检测帧头 0x55 0xAA 0xEB 0x90
    if (!frameStarted && pData[i] == 0x55) {
      frameIndex = 0;
      frameBuffer[frameIndex++] = pData[i];
      frameStarted = true;
    } else if (frameStarted) {
      if (frameIndex == 1 && pData[i] != 0xAA) {
        frameStarted = false;
        frameIndex = 0;
        continue;
      }
      if (frameIndex == 2 && pData[i] != 0xEB) {
        frameStarted = false;
        frameIndex = 0;
        continue;
      }
      if (frameIndex == 3 && pData[i] != 0x90) {
        frameStarted = false;
        frameIndex = 0;
        continue;
      }
      
      frameBuffer[frameIndex++] = pData[i];
      
      if (frameIndex >= 300) {
        // 验证CRC
        uint8_t crc = calculateCRC(frameBuffer, 299);
        if (crc == frameBuffer[299]) {
          newDataAvailable = true;
        }
        frameStarted = false;
        frameIndex = 0;
      }
      
      if (frameIndex >= 320) {
        frameStarted = false;
        frameIndex = 0;
      }
    }
  }
}

// ==================== 数据解析 ====================
void parseJK02_32S_Frame() {
  if (!newDataAvailable) return;
  newDataAvailable = false;
  
  uint8_t frameType = frameBuffer[4];
  
  if (frameType == 0x02) {
    // 电池信息帧
    // 单体电压 (6-69字节, 最多32串)
    bmsData.cellCount = 0;
    float totalCellVolt = 0;
    for (int i = 0; i < 24; i++) {
      uint16_t raw = frameBuffer[6 + i*2] | (frameBuffer[7 + i*2] << 8);
      bmsData.cellVoltages[i] = raw * 0.001f;
      if (raw > 0) {
        bmsData.cellCount++;
        totalCellVolt += bmsData.cellVoltages[i];
      }
    }
    
    if (bmsData.cellCount > 0) {
      bmsData.avgCellVoltage = totalCellVolt / bmsData.cellCount;
    }
    
    // 平均单体电压 (74-75)
    bmsData.avgCellVoltage = (frameBuffer[74] | (frameBuffer[75] << 8)) * 0.001f;
    
    // 压差 (76-77)
    bmsData.deltaCellVoltage = (frameBuffer[76] | (frameBuffer[77] << 8)) * 0.001f;
    
    // MOS管温度 (144-145)
    bmsData.mosTemp = (int16_t)(frameBuffer[144] | (frameBuffer[145] << 8)) * 0.1f;
    
    // 总电压 (150-153) - uint32 little-endian
    bmsData.batteryVoltage = ((uint32_t)frameBuffer[150] | 
                              ((uint32_t)frameBuffer[151] << 8) | 
                              ((uint32_t)frameBuffer[152] << 16) | 
                              ((uint32_t)frameBuffer[153] << 24)) * 0.001f;
    
    // 功率 (154-157)
    bmsData.batteryPower = ((uint32_t)frameBuffer[154] | 
                            ((uint32_t)frameBuffer[155] << 8) | 
                            ((uint32_t)frameBuffer[156] << 16) | 
                            ((uint32_t)frameBuffer[157] << 24)) * 0.001f;
    
    // 电流 (158-161) - 有符号
    int32_t rawCurrent = (int32_t)((uint32_t)frameBuffer[158] | 
                                   ((uint32_t)frameBuffer[159] << 8) | 
                                   ((uint32_t)frameBuffer[160] << 16) | 
                                   ((uint32_t)frameBuffer[161] << 24));
    bmsData.chargeCurrent = rawCurrent * 0.001f;
    
    // 重新计算功率 = 电压 * 电流
    bmsData.batteryPower = bmsData.batteryVoltage * bmsData.chargeCurrent;
    targetPower = bmsData.batteryPower;
    
    // 温度传感器1 (162-163)
    bmsData.tempSensor1 = (int16_t)(frameBuffer[162] | (frameBuffer[163] << 8)) * 0.1f;
    
    // 温度传感器2 (164-165)
    bmsData.tempSensor2 = (int16_t)(frameBuffer[164] | (frameBuffer[165] << 8)) * 0.1f;
    
    // 错误码 (166-167)
    bmsData.errors = frameBuffer[166] | (frameBuffer[167] << 8);
    
    // SOC (173)
    bmsData.soc = frameBuffer[173];
    
    // 剩余容量 (174-177)
    bmsData.remainingCapacity = ((uint32_t)frameBuffer[174] | 
                                 ((uint32_t)frameBuffer[175] << 8) | 
                                 ((uint32_t)frameBuffer[176] << 16) | 
                                 ((uint32_t)frameBuffer[177] << 24)) * 0.001f;
    
    // 标称容量 (178-181)
    bmsData.nominalCapacity = ((uint32_t)frameBuffer[178] | 
                               ((uint32_t)frameBuffer[179] << 8) | 
                               ((uint32_t)frameBuffer[180] << 16) | 
                               ((uint32_t)frameBuffer[181] << 24)) * 0.001f;
    
    // 循环次数 (182-185)
    bmsData.cycleCount = ((uint32_t)frameBuffer[182] | 
                          ((uint32_t)frameBuffer[183] << 8) | 
                          ((uint32_t)frameBuffer[184] << 16) | 
                          ((uint32_t)frameBuffer[185] << 24));
    
    // SOH (190)
    bmsData.soh = frameBuffer[190];
    
    // MOS状态
    bmsData.chargeMosOn = frameBuffer[198] == 1;
    bmsData.dischargeMosOn = frameBuffer[199] == 1;
    bmsData.balancing = frameBuffer[201] == 1;
    
    Serial.printf("电压: %.2fV, 电流: %.2fA, 功率: %.1fW, SOC: %d%%\n",
                  bmsData.batteryVoltage, bmsData.chargeCurrent, 
                  bmsData.batteryPower, bmsData.soc);
  }
}

// ==================== BLE连接 ====================
bool connectToBMS() {
  Serial.println("开始扫描BMS...");
  
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new BMSAdvertisedCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10);
  
  if (!bleFound) {
    Serial.println("未找到BMS设备");
    return false;
  }
  
  Serial.println("连接到BMS...");
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new BMSClientCallbacks());
  pClient->setConnectionParams(12, 12, 0, 150);
  pClient->setConnectTimeout(5000);
  
  NimBLEAddress addr(BMS_MAC_ADDRESS);
  if (!pClient->connect(addr)) {
    Serial.println("连接失败!");
    return false;
  }
  
  Serial.println("已连接，查找服务...");
  NimBLERemoteService* pService = pClient->getService(SERVICE_UUID);
  if (!pService) {
    Serial.println("未找到服务!");
    return false;
  }
  
  pRemoteCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
  if (!pRemoteCharacteristic) {
    Serial.println("未找到特征!");
    return false;
  }
  
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->subscribe(true, notifyCallback);
    Serial.println("已订阅通知");
  }
  
  // 发送初始化命令
  delay(500);
  uint8_t cmdDeviceInfo[20] = {0xAA, 0x55, 0x90, 0xEB, 0x97, 0x00};
  for (int i = 6; i < 19; i++) cmdDeviceInfo[i] = 0x00;
  cmdDeviceInfo[19] = calculateCRC(cmdDeviceInfo, 19);
  pRemoteCharacteristic->writeValue((uint8_t*)cmdDeviceInfo, 20);
  
  delay(500);
  uint8_t cmdCellInfo[20] = {0xAA, 0x55, 0x90, 0xEB, 0x96, 0x00};
  for (int i = 6; i < 19; i++) cmdCellInfo[i] = 0x00;
  cmdCellInfo[19] = calculateCRC(cmdCellInfo, 19);
  pRemoteCharacteristic->writeValue((uint8_t*)cmdCellInfo, 20);
  
  Serial.println("初始化命令已发送");
  return true;
}

// ==================== UI绘制 ====================
void drawBluetoothIcon(int x, int y, bool connected) {
  uint16_t color = connected ? COLOR_BT_CONNECTED : COLOR_BT_DISCONN;
  
  // 绘制蓝牙符号
  tft.drawLine(x+3, y, x+3, y+10, color);
  tft.drawLine(x, y+3, x+6, y+7, color);
  tft.drawLine(x, y+7, x+6, y+3, color);
  tft.drawLine(x, y+3, x, y+7, color);
  tft.drawLine(x+6, y+3, x+6, y+7, color);
  
  // 状态点
  if (connected) {
    tft.fillCircle(x+10, y+5, 2, COLOR_BT_CONNECTED);
  } else {
    tft.drawCircle(x+10, y+5, 2, COLOR_BT_DISCONN);
  }
}

void drawPowerBar(int x, int y, int w, int h, float power, float maxPower) {
  float ratio = constrain(abs(power) / maxPower, 0, 1);
  int fillW = (int)(w * ratio);
  
  uint16_t color = getPowerColor(power);
  
  // 背景条
  tft.fillRoundRect(x, y, w, h, h/2, COLOR_CARD_BORDER);
  
  // 填充条
  if (fillW > 0) {
    tft.fillRoundRect(x, y, fillW, h, h/2, color);
    // 高光
    tft.drawFastHLine(x+2, y+1, fillW-4, 0xFFFF);
  }
}

void drawBatteryIcon(int x, int y, int w, int h, uint8_t soc, bool charging) {
  uint16_t color;
  if (soc > 60) color = COLOR_POWER_LOW;
  else if (soc > 30) color = COLOR_POWER_MID;
  else color = COLOR_POWER_MAX;
  
  // 电池外壳
  tft.drawRoundRect(x, y, w, h, 2, COLOR_TEXT_SECOND);
  tft.drawRect(x+w, y+3, 3, h-6, COLOR_TEXT_SECOND);
  
  // 填充
  int fillW = ((w-4) * soc) / 100;
  if (fillW > 0) {
    tft.fillRoundRect(x+2, y+2, fillW, h-4, 1, color);
  }
  
  // 充电闪电
  if (charging) {
    tft.drawLine(x+w/2-2, y+3, x+w/2+1, y+h/2, COLOR_CHARGE);
    tft.drawLine(x+w/2+1, y+h/2, x+w/2-1, y+h/2, COLOR_CHARGE);
    tft.drawLine(x+w/2-1, y+h/2, x+w/2+2, y+h-4, COLOR_CHARGE);
  }
}

void drawMainPowerDisplay() {
  int cx = 160;
  
  // 功率卡片背景
  drawCard(10, 25, 300, 110, 12, COLOR_CARD_BG, COLOR_CARD_BORDER, true);
  
  // 标题
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.drawString("实时功率", cx, 32, 2);
  
  // 功率数值
  tft.setTextDatum(TC_DATUM);
  uint16_t powerColor = getPowerColor(displayPower);
  
  // 清除之前的数值区域
  tft.fillRect(60, 48, 200, 40, COLOR_CARD_BG);
  
  if (abs(displayPower) >= 1000) {
    tft.setTextColor(powerColor, COLOR_CARD_BG);
    tft.setTextSize(2);
    tft.drawFloat(abs(displayPower), 1, cx, 52, 4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
    tft.drawString("W", cx + 90, 62, 2);
  } else {
    tft.setTextColor(powerColor, COLOR_CARD_BG);
    tft.setTextSize(2);
    tft.drawNumber((int)abs(displayPower), cx, 52, 4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
    tft.drawString("W", cx + 70, 62, 2);
  }
  
  // 充放电状态
  tft.setTextDatum(TC_DATUM);
  tft.fillRect(130, 92, 60, 16, COLOR_CARD_BG);
  if (displayPower > 10) {
    tft.setTextColor(COLOR_CHARGE, COLOR_CARD_BG);
    tft.drawString("充电中", cx, 95, 2);
  } else if (displayPower < -10) {
    tft.setTextColor(COLOR_DISCHARGE, COLOR_CARD_BG);
    tft.drawString("放电中", cx, 95, 2);
  } else {
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
    tft.drawString("待机", cx, 95, 2);
  }
  
  // 功率条
  drawPowerBar(30, 112, 260, 10, displayPower, 3000);
}

void drawCapacityDisplay() {
  // 容量卡片
  drawCard(10, 142, 145, 55, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("电池容量", 20, 148, 2);
  
  // SOC大数字
  tft.setTextColor(COLOR_ACCENT, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.drawNumber(bmsData.soc, 20, 164, 4);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("%", 65, 168, 2);
  
  // 容量详情
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  char capStr[32];
  sprintf(capStr, "%.1f/%.1fAh", bmsData.remainingCapacity, bmsData.nominalCapacity);
  tft.drawString(capStr, 145, 168, 2);
  
  // 电池图标
  drawBatteryIcon(115, 148, 30, 14, bmsData.soc, bmsData.chargeMosOn && bmsData.chargeCurrent > 0.1);
}

void drawVoltageCurrentDisplay() {
  // 电压电流卡片
  drawCard(165, 142, 145, 55, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("电压/电流", 175, 148, 2);
  
  // 电压
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  char voltStr[16];
  sprintf(voltStr, "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, 175, 164, 4);
  
  // 电流
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  char currStr[16];
  sprintf(currStr, "%.1fA", bmsData.chargeCurrent);
  tft.drawString(currStr, 300, 164, 4);
}

void drawTemperatureDisplay() {
  // 温度卡片
  drawCard(10, 202, 145, 32, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("温度", 20, 206, 2);
  
  char tempStr[32];
  sprintf(tempStr, "%.1fC", bmsData.tempSensor1);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_CARD_BG);
  tft.drawString(tempStr, 55, 206, 4);
  
  // MOS温度
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  sprintf(tempStr, "MOS:%.0fC", bmsData.mosTemp);
  tft.drawString(tempStr, 145, 210, 2);
}

void drawStatusDisplay() {
  // 状态卡片
  drawCard(165, 202, 145, 32, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("状态", 175, 206, 2);
  
  // MOS状态指示
  int x = 200;
  if (bmsData.chargeMosOn) {
    tft.fillCircle(x, 216, 3, COLOR_CHARGE);
    tft.setTextColor(COLOR_CHARGE, COLOR_CARD_BG);
    tft.drawString("充", x+8, 212, 2);
  } else {
    tft.drawCircle(x, 216, 3, COLOR_TEXT_DIM);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
    tft.drawString("充", x+8, 212, 2);
  }
  
  x += 30;
  if (bmsData.dischargeMosOn) {
    tft.fillCircle(x, 216, 3, COLOR_DISCHARGE);
    tft.setTextColor(COLOR_DISCHARGE, COLOR_CARD_BG);
    tft.drawString("放", x+8, 212, 2);
  } else {
    tft.drawCircle(x, 216, 3, COLOR_TEXT_DIM);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
    tft.drawString("放", x+8, 212, 2);
  }
  
  x += 30;
  if (bmsData.balancing) {
    tft.fillCircle(x, 216, 3, COLOR_POWER_MID);
    tft.setTextColor(COLOR_POWER_MID, COLOR_CARD_BG);
    tft.drawString("均", x+8, 212, 2);
  } else {
    tft.drawCircle(x, 216, 3, COLOR_TEXT_DIM);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
    tft.drawString("均", x+8, 212, 2);
  }
}

void drawHeader() {
  // 顶部栏背景
  tft.fillRect(0, 0, 320, 22, COLOR_BG);
  
  // 标题
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("JK-BMS 监控", 10, 4, 2);
  
  // 蓝牙状态
  drawBluetoothIcon(290, 4, bleConnected);
  
  // 分隔线
  tft.drawFastHLine(0, 21, 320, COLOR_CARD_BORDER);
}

void drawFooter() {
  // 底部信息
  tft.fillRect(0, 238, 320, 2, COLOR_CARD_BORDER);
}

void updateDisplay() {
  // 功率动画插值
  displayPower += (targetPower - displayPower) * 0.3;
  
  // 检测蓝牙状态变化
  if (bleConnected != lastBleState || firstDraw) {
    drawHeader();
    lastBleState = bleConnected;
  }
  
  // 主功率显示 (每次刷新)
  drawMainPowerDisplay();
  
  // 其他信息 (首次绘制或数据变化时)
  if (firstDraw || bmsData.soc != lastSoc) {
    drawCapacityDisplay();
    drawVoltageCurrentDisplay();
    drawTemperatureDisplay();
    drawStatusDisplay();
    drawFooter();
    lastSoc = bmsData.soc;
  }
  
  firstDraw = false;
  animationFrame++;
}

// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  Serial.println("JKBMS Display 启动...");
  
  // 初始化显示屏
  tft.init();
  tft.setRotation(1); // 横屏
  tft.fillScreen(COLOR_BG);
  
  // 显示启动画面
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("JK-BMS", 160, 100, 4);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("战斗模式启动中...", 160, 130, 2);
  
  // 初始化BLE
  NimBLEDevice::init("JKBMS-Monitor");
  
  delay(1000);
  tft.fillScreen(COLOR_BG);
  firstDraw = true;
}

// ==================== 主循环 ====================
void loop() {
  // 蓝牙连接管理
  if (!bleConnected) {
    if (millis() - lastBleActivity > 10000) {
      connectToBMS();
      lastBleActivity = millis();
    }
  }
  
  // 解析数据
  parseJK02_32S_Frame();
  
  // 更新UI (限制刷新率)
  if (millis() - lastUiUpdate > 100) { // 10fps
    updateDisplay();
    lastUiUpdate = millis();
  }
  
  delay(10);
}