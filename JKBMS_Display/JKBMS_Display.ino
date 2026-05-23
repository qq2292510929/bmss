/*
  JKBMS Bluetooth Monitor for ESP32-32E + ST7789 2.8" Display
  战斗风格UI - 汽车运动模式主题 + 小米SU7弹射模式急加速动画
  
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

// ==================== 战斗风格颜色主题 ====================
#define COLOR_BG            0x0000
#define COLOR_CARD_BG       0x18E3
#define COLOR_CARD_BORDER   0x39E7

#define COLOR_POWER_LOW     0x07E0
#define COLOR_POWER_MID     0xFFE0
#define COLOR_POWER_HIGH    0xFBE0
#define COLOR_POWER_MAX     0xF800

#define COLOR_ACCENT        0x05D9
#define COLOR_ACCENT_DIM    0x02A8
#define COLOR_TEXT_PRIMARY  0xFFFF
#define COLOR_TEXT_SECOND   0xBDF7
#define COLOR_TEXT_DIM      0x7BEF

#define COLOR_BT_CONNECTED  0x07E0
#define COLOR_BT_DISCONN    0xF800
#define COLOR_CHARGE        0x07FF
#define COLOR_DISCHARGE     0xFBE0

// 弹射模式颜色
#define COLOR_BOOST_GLOW    0xF800
#define COLOR_BOOST_CORE    0xFFE0
#define COLOR_SPEED_LINE    0x7BEF

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
  float batteryVoltage = 0;
  float chargeCurrent = 0;
  float batteryPower = 0;
  uint8_t soc = 0;
  float remainingCapacity = 0;
  float nominalCapacity = 0;
  float mosTemp = 0;
  float tempSensor1 = 0;
  float tempSensor2 = 0;
  bool chargeMosOn = false;
  bool dischargeMosOn = false;
  bool balancing = false;
  uint16_t errors = 0;
  float cellVoltages[24] = {0};
  uint8_t cellCount = 0;
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  uint32_t cycleCount = 0;
  float cycleCapacity = 0;
  uint8_t soh = 100;
} bmsData;

// UI状态
float displayPower = 0;
float targetPower = 0;
float lastTargetPower = 0;
unsigned long lastUiUpdate = 0;
unsigned long lastBleActivity = 0;
uint8_t animationFrame = 0;

// 部分刷新优化
bool firstDraw = true;
uint8_t lastSoc = 255;
bool lastBleState = false;

// ==================== 弹射模式动画系统 ====================
// 速度线结构
struct SpeedLine {
  float x;
  float y;
  float length;
  float speed;
  bool active;
  uint16_t color;
};

#define MAX_SPEED_LINES 12
SpeedLine speedLines[MAX_SPEED_LINES];

// 粒子结构 (用于功率爆发效果)
struct Particle {
  float x;
  float y;
  float vx;
  float vy;
  float life;
  float maxLife;
  uint16_t color;
  bool active;
};

#define MAX_PARTICLES 20
Particle particles[MAX_PARTICLES];

// 弹射模式状态
bool boostMode = false;
float boostIntensity = 0;
float powerAcceleration = 0;
unsigned long boostStartTime = 0;
#define BOOST_THRESHOLD 300
#define BOOST_FADE_TIME 2000

// 背景脉冲
float bgPulse = 0;
float bgPulseSpeed = 0;

// ==================== 工具函数 ====================
uint8_t calculateCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) crc += data[i];
  return crc;
}

uint16_t getPowerColor(float power) {
  float absPower = abs(power);
  if (absPower < 100) return COLOR_POWER_LOW;
  if (absPower < 500) return COLOR_POWER_MID;
  if (absPower < 1000) return COLOR_POWER_HIGH;
  return COLOR_POWER_MAX;
}

uint16_t blendColor(uint16_t c1, uint16_t c2, float ratio) {
  uint8_t r1 = (c1 >> 11) & 0x1F;
  uint8_t g1 = (c1 >> 5) & 0x3F;
  uint8_t b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F;
  uint8_t g2 = (c2 >> 5) & 0x3F;
  uint8_t b2 = c2 & 0x1F;
  uint8_t r = r1 + (r2 - r1) * ratio;
  uint8_t g = g1 + (g2 - g1) * ratio;
  uint8_t b = b1 + (b2 - b1) * ratio;
  return (r << 11) | (g << 5) | b;
}

// ==================== 弹射模式动画系统 ====================
void initSpeedLines() {
  for (int i = 0; i < MAX_SPEED_LINES; i++) {
    speedLines[i].active = false;
  }
}

void initParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    particles[i].active = false;
  }
}

void spawnSpeedLine() {
  for (int i = 0; i < MAX_SPEED_LINES; i++) {
    if (!speedLines[i].active) {
      speedLines[i].x = random(320);
      speedLines[i].y = random(240);
      speedLines[i].length = random(10, 40) * (1 + boostIntensity);
      speedLines[i].speed = random(5, 15) * (1 + boostIntensity * 2);
      speedLines[i].active = true;
      speedLines[i].color = blendColor(COLOR_SPEED_LINE, COLOR_BOOST_GLOW, boostIntensity);
      break;
    }
  }
}

void spawnParticle(int cx, int cy, uint16_t color) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active) {
      particles[i].x = cx + random(-30, 30);
      particles[i].y = cy + random(-20, 20);
      float angle = random(0, 628) / 100.0;
      float speed = random(20, 80) * (1 + boostIntensity);
      particles[i].vx = cos(angle) * speed;
      particles[i].vy = sin(angle) * speed;
      particles[i].life = 1.0;
      particles[i].maxLife = random(10, 30);
      particles[i].color = color;
      particles[i].active = true;
      break;
    }
  }
}

void updateSpeedLines() {
  for (int i = 0; i < MAX_SPEED_LINES; i++) {
    if (speedLines[i].active) {
      // 速度线向右移动 (模拟前进感)
      speedLines[i].x += speedLines[i].speed;
      if (speedLines[i].x > 340) {
        speedLines[i].active = false;
      }
    }
  }
}

void updateParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active) {
      particles[i].x += particles[i].vx * 0.1;
      particles[i].y += particles[i].vy * 0.1;
      particles[i].vx *= 0.95;
      particles[i].vy *= 0.95;
      particles[i].life -= 1.0 / particles[i].maxLife;
      if (particles[i].life <= 0) {
        particles[i].active = false;
      }
    }
  }
}

void drawSpeedLines() {
  for (int i = 0; i < MAX_SPEED_LINES; i++) {
    if (speedLines[i].active) {
      int alpha = (int)(255 * speedLines[i].active);
      int x1 = (int)speedLines[i].x;
      int y1 = (int)speedLines[i].y;
      int x2 = (int)(speedLines[i].x - speedLines[i].length);
      int y2 = y1;
      
      // 绘制速度线 (带渐变)
      for (int j = 0; j < 3; j++) {
        uint16_t lineColor = blendColor(speedLines[i].color, COLOR_BG, (float)j / 3);
        tft.drawLine(x1, y1 + j - 1, x2, y2 + j - 1, lineColor);
      }
    }
  }
}

void drawParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (particles[i].active && particles[i].life > 0) {
      int size = (int)(3 * particles[i].life);
      if (size > 0) {
        uint16_t pColor = blendColor(particles[i].color, COLOR_BG, 1 - particles[i].life);
        tft.fillCircle((int)particles[i].x, (int)particles[i].y, size, pColor);
      }
    }
  }
}

void drawBoostBackground() {
  if (boostIntensity <= 0.01) return;
  
  // 背景脉冲效果
  int pulseSize = (int)(20 * bgPulse * boostIntensity);
  uint16_t pulseColor = blendColor(COLOR_BG, COLOR_BOOST_GLOW, bgPulse * boostIntensity * 0.3);
  
  // 在功率卡片周围绘制脉冲光环
  int cx = 160, cy = 80;
  for (int r = 60; r < 80 + pulseSize; r += 4) {
    float alpha = 1.0 - (float)(r - 60) / (20 + pulseSize);
    uint16_t ringColor = blendColor(COLOR_BG, COLOR_BOOST_GLOW, alpha * boostIntensity * 0.2);
    tft.drawCircle(cx, cy, r, ringColor);
  }
  
  // 绘制径向速度线 (从中心向外)
  int numRays = 8;
  for (int i = 0; i < numRays; i++) {
    float angle = (2 * PI * i / numRays) + (animationFrame * 0.1);
    int x1 = cx + (int)(cos(angle) * 70);
    int y1 = cy + (int)(sin(angle) * 50);
    int x2 = cx + (int)(cos(angle) * (90 + pulseSize));
    int y2 = cy + (int)(sin(angle) * (70 + pulseSize * 0.7));
    uint16_t rayColor = blendColor(COLOR_BG, COLOR_BOOST_CORE, boostIntensity * 0.4);
    tft.drawLine(x1, y1, x2, y2, rayColor);
  }
}

void updateBoostMode() {
  // 计算功率加速度 (功率变化率)
  float powerDelta = targetPower - lastTargetPower;
  powerAcceleration = powerDelta;
  lastTargetPower = targetPower;
  
  // 检测急加速 (功率快速增加，负值表示放电，所以放电功率快速增加是变得更负)
  float absPower = abs(targetPower);
  float absLastPower = abs(lastTargetPower);
  
  // 检测是否进入弹射模式
  bool rapidAcceleration = false;
  if (targetPower < -10) { // 放电状态
    if (absPower > absLastPower + BOOST_THRESHOLD) {
      rapidAcceleration = true;
    }
  }
  
  if (rapidAcceleration && !boostMode) {
    boostMode = true;
    boostStartTime = millis();
    boostIntensity = 1.0;
    
    // 爆发粒子效果
    for (int i = 0; i < 10; i++) {
      spawnParticle(160, 80, COLOR_BOOST_CORE);
    }
  }
  
  // 更新弹射模式强度
  if (boostMode) {
    unsigned long elapsed = millis() - boostStartTime;
    if (elapsed < BOOST_FADE_TIME) {
      boostIntensity = 1.0 - ((float)elapsed / BOOST_FADE_TIME);
      // 如果功率仍然很高，保持一定强度
      if (abs(targetPower) > 1000) {
        boostIntensity = max(boostIntensity, 0.5);
      }
    } else {
      boostMode = false;
      boostIntensity = 0;
    }
  }
  
  // 更新背景脉冲
  bgPulseSpeed = 0.1 + boostIntensity * 0.3;
  bgPulse += bgPulseSpeed;
  if (bgPulse > 1) bgPulse = 0;
  
  // 随机生成速度线
  if (boostIntensity > 0.1) {
    int spawnChance = (int)(boostIntensity * 5);
    for (int i = 0; i < spawnChance; i++) {
      if (random(100) < 30) spawnSpeedLine();
    }
  }
  
  // 高功率时持续生成粒子
  if (abs(targetPower) > 500 && random(100) < 20 * boostIntensity) {
    spawnParticle(160 + random(-40, 40), 80 + random(-20, 20), getPowerColor(targetPower));
  }
  
  updateSpeedLines();
  updateParticles();
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
    if (!frameStarted && pData[i] == 0x55) {
      frameIndex = 0;
      frameBuffer[frameIndex++] = pData[i];
      frameStarted = true;
    } else if (frameStarted) {
      if (frameIndex == 1 && pData[i] != 0xAA) { frameStarted = false; frameIndex = 0; continue; }
      if (frameIndex == 2 && pData[i] != 0xEB) { frameStarted = false; frameIndex = 0; continue; }
      if (frameIndex == 3 && pData[i] != 0x90) { frameStarted = false; frameIndex = 0; continue; }
      frameBuffer[frameIndex++] = pData[i];
      if (frameIndex >= 300) {
        uint8_t crc = calculateCRC(frameBuffer, 299);
        if (crc == frameBuffer[299]) newDataAvailable = true;
        frameStarted = false;
        frameIndex = 0;
      }
      if (frameIndex >= 320) { frameStarted = false; frameIndex = 0; }
    }
  }
}

// ==================== 数据解析 ====================
void parseJK02_32S_Frame() {
  if (!newDataAvailable) return;
  newDataAvailable = false;
  uint8_t frameType = frameBuffer[4];
  
  if (frameType == 0x02) {
    bmsData.cellCount = 0;
    float totalCellVolt = 0;
    for (int i = 0; i < 24; i++) {
      uint16_t raw = frameBuffer[6 + i*2] | (frameBuffer[7 + i*2] << 8);
      bmsData.cellVoltages[i] = raw * 0.001f;
      if (raw > 0) { bmsData.cellCount++; totalCellVolt += bmsData.cellVoltages[i]; }
    }
    if (bmsData.cellCount > 0) bmsData.avgCellVoltage = totalCellVolt / bmsData.cellCount;
    bmsData.avgCellVoltage = (frameBuffer[74] | (frameBuffer[75] << 8)) * 0.001f;
    bmsData.deltaCellVoltage = (frameBuffer[76] | (frameBuffer[77] << 8)) * 0.001f;
    bmsData.mosTemp = (int16_t)(frameBuffer[144] | (frameBuffer[145] << 8)) * 0.1f;
    bmsData.batteryVoltage = ((uint32_t)frameBuffer[150] | ((uint32_t)frameBuffer[151] << 8) | 
                              ((uint32_t)frameBuffer[152] << 16) | ((uint32_t)frameBuffer[153] << 24)) * 0.001f;
    int32_t rawCurrent = (int32_t)((uint32_t)frameBuffer[158] | ((uint32_t)frameBuffer[159] << 8) | 
                                   ((uint32_t)frameBuffer[160] << 16) | ((uint32_t)frameBuffer[161] << 24));
    bmsData.chargeCurrent = rawCurrent * 0.001f;
    bmsData.batteryPower = bmsData.batteryVoltage * bmsData.chargeCurrent;
    targetPower = bmsData.batteryPower;
    bmsData.tempSensor1 = (int16_t)(frameBuffer[162] | (frameBuffer[163] << 8)) * 0.1f;
    bmsData.tempSensor2 = (int16_t)(frameBuffer[164] | (frameBuffer[165] << 8)) * 0.1f;
    bmsData.errors = frameBuffer[166] | (frameBuffer[167] << 8);
    bmsData.soc = frameBuffer[173];
    bmsData.remainingCapacity = ((uint32_t)frameBuffer[174] | ((uint32_t)frameBuffer[175] << 8) | 
                                 ((uint32_t)frameBuffer[176] << 16) | ((uint32_t)frameBuffer[177] << 24)) * 0.001f;
    bmsData.nominalCapacity = ((uint32_t)frameBuffer[178] | ((uint32_t)frameBuffer[179] << 8) | 
                               ((uint32_t)frameBuffer[180] << 16) | ((uint32_t)frameBuffer[181] << 24)) * 0.001f;
    bmsData.cycleCount = ((uint32_t)frameBuffer[182] | ((uint32_t)frameBuffer[183] << 8) | 
                          ((uint32_t)frameBuffer[184] << 16) | ((uint32_t)frameBuffer[185] << 24));
    bmsData.soh = frameBuffer[190];
    bmsData.chargeMosOn = frameBuffer[198] == 1;
    bmsData.dischargeMosOn = frameBuffer[199] == 1;
    bmsData.balancing = frameBuffer[201] == 1;
    Serial.printf("电压: %.2fV, 电流: %.2fA, 功率: %.1fW, SOC: %d%%\n",
                  bmsData.batteryVoltage, bmsData.chargeCurrent, bmsData.batteryPower, bmsData.soc);
  }
}

// ==================== BLE连接 ====================
bool connectToBMS() {
  Serial.println("开始扫描BMS...");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new BMSAdvertisedCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10);
  if (!bleFound) { Serial.println("未找到BMS设备"); return false; }
  
  Serial.println("连接到BMS...");
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new BMSClientCallbacks());
  pClient->setConnectionParams(12, 12, 0, 150);
  pClient->setConnectTimeout(5000);
  
  NimBLEAddress addr(BMS_MAC_ADDRESS);
  if (!pClient->connect(addr)) { Serial.println("连接失败!"); return false; }
  
  NimBLERemoteService* pService = pClient->getService(SERVICE_UUID);
  if (!pService) { Serial.println("未找到服务!"); return false; }
  pRemoteCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
  if (!pRemoteCharacteristic) { Serial.println("未找到特征!"); return false; }
  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->subscribe(true, notifyCallback);
    Serial.println("已订阅通知");
  }
  
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
  tft.drawLine(x+3, y, x+3, y+10, color);
  tft.drawLine(x, y+3, x+6, y+7, color);
  tft.drawLine(x, y+7, x+6, y+3, color);
  tft.drawLine(x, y+3, x, y+7, color);
  tft.drawLine(x+6, y+3, x+6, y+7, color);
  if (connected) tft.fillCircle(x+10, y+5, 2, COLOR_BT_CONNECTED);
  else tft.drawCircle(x+10, y+5, 2, COLOR_BT_DISCONN);
}

void drawPowerBar(int x, int y, int w, int h, float power, float maxPower) {
  float ratio = constrain(abs(power) / maxPower, 0, 1);
  int fillW = (int)(w * ratio);
  uint16_t color = getPowerColor(power);
  tft.fillRoundRect(x, y, w, h, h/2, COLOR_CARD_BORDER);
  if (fillW > 0) {
    tft.fillRoundRect(x, y, fillW, h, h/2, color);
    tft.drawFastHLine(x+2, y+1, fillW-4, 0xFFFF);
  }
}

void drawBatteryIcon(int x, int y, int w, int h, uint8_t soc, bool charging) {
  uint16_t color;
  if (soc > 60) color = COLOR_POWER_LOW;
  else if (soc > 30) color = COLOR_POWER_MID;
  else color = COLOR_POWER_MAX;
  tft.drawRoundRect(x, y, w, h, 2, COLOR_TEXT_SECOND);
  tft.drawRect(x+w, y+3, 3, h-6, COLOR_TEXT_SECOND);
  int fillW = ((w-4) * soc) / 100;
  if (fillW > 0) tft.fillRoundRect(x+2, y+2, fillW, h-4, 1, color);
  if (charging) {
    tft.drawLine(x+w/2-2, y+3, x+w/2+1, y+h/2, COLOR_CHARGE);
    tft.drawLine(x+w/2+1, y+h/2, x+w/2-1, y+h/2, COLOR_CHARGE);
    tft.drawLine(x+w/2-1, y+h/2, x+w/2+2, y+h-4, COLOR_CHARGE);
  }
}

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, 
              uint16_t bgColor, uint16_t borderColor, bool glow = false) {
  if (glow) {
    tft.drawRoundRect(x-1, y-1, w+2, h+2, r+1, COLOR_ACCENT_DIM);
    tft.drawRoundRect(x-2, y-2, w+4, h+4, r+2, 0x0144);
  }
  tft.fillRoundRect(x, y, w, h, r, bgColor);
  tft.drawRoundRect(x, y, w, h, r, borderColor);
  tft.drawFastHLine(x+r, y, w-r*2, 0x4A69);
}

void drawMainPowerDisplay() {
  int cx = 160;
  
  // 根据弹射模式强度调整卡片背景色
  uint16_t cardBg = COLOR_CARD_BG;
  if (boostIntensity > 0.1) {
    cardBg = blendColor(COLOR_CARD_BG, COLOR_BOOST_GLOW, boostIntensity * 0.15);
  }
  
  drawCard(10, 25, 300, 110, 12, cardBg, COLOR_CARD_BORDER, true);
  
  tft.setTextColor(COLOR_TEXT_SECOND, cardBg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.drawString("实时功率", cx, 32, 2);
  
  tft.setTextDatum(TC_DATUM);
  uint16_t powerColor = getPowerColor(displayPower);
  
  // 弹射模式下颜色更亮
  if (boostIntensity > 0.3) {
    powerColor = blendColor(powerColor, COLOR_BOOST_CORE, boostIntensity * 0.5);
  }
  
  tft.fillRect(60, 48, 200, 40, cardBg);
  
  if (abs(displayPower) >= 1000) {
    tft.setTextColor(powerColor, cardBg);
    tft.setTextSize(2);
    tft.drawFloat(abs(displayPower), 1, cx, 52, 4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SECOND, cardBg);
    tft.drawString("W", cx + 90, 62, 2);
  } else {
    tft.setTextColor(powerColor, cardBg);
    tft.setTextSize(2);
    tft.drawNumber((int)abs(displayPower), cx, 52, 4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SECOND, cardBg);
    tft.drawString("W", cx + 70, 62, 2);
  }
  
  tft.setTextDatum(TC_DATUM);
  tft.fillRect(130, 92, 60, 16, cardBg);
  if (displayPower > 10) {
    tft.setTextColor(COLOR_CHARGE, cardBg);
    tft.drawString("充电中", cx, 95, 2);
  } else if (displayPower < -10) {
    uint16_t statusColor = boostIntensity > 0.3 ? COLOR_BOOST_CORE : COLOR_DISCHARGE;
    tft.setTextColor(statusColor, cardBg);
    tft.drawString(boostIntensity > 0.3 ? "弹射模式!" : "放电中", cx, 95, 2);
  } else {
    tft.setTextColor(COLOR_TEXT_DIM, cardBg);
    tft.drawString("待机", cx, 95, 2);
  }
  
  drawPowerBar(30, 112, 260, 10, displayPower, 3000);
}

void drawCapacityDisplay() {
  drawCard(10, 142, 145, 55, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("电池容量", 20, 148, 2);
  tft.setTextColor(COLOR_ACCENT, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(2);
  tft.drawNumber(bmsData.soc, 20, 164, 4);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("%", 65, 168, 2);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  char capStr[32];
  sprintf(capStr, "%.1f/%.1fAh", bmsData.remainingCapacity, bmsData.nominalCapacity);
  tft.drawString(capStr, 145, 168, 2);
  drawBatteryIcon(115, 148, 30, 14, bmsData.soc, bmsData.chargeMosOn && bmsData.chargeCurrent > 0.1);
}

void drawVoltageCurrentDisplay() {
  drawCard(165, 142, 145, 55, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("电压/电流", 175, 148, 2);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  char voltStr[16];
  sprintf(voltStr, "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, 175, 164, 4);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  char currStr[16];
  sprintf(currStr, "%.1fA", bmsData.chargeCurrent);
  tft.drawString(currStr, 300, 164, 4);
}

void drawTemperatureDisplay() {
  drawCard(10, 202, 145, 32, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("温度", 20, 206, 2);
  char tempStr[32];
  sprintf(tempStr, "%.1fC", bmsData.tempSensor1);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_CARD_BG);
  tft.drawString(tempStr, 55, 206, 4);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_BG);
  tft.setTextDatum(TR_DATUM);
  sprintf(tempStr, "MOS:%.0fC", bmsData.mosTemp);
  tft.drawString(tempStr, 145, 210, 2);
}

void drawStatusDisplay() {
  drawCard(165, 202, 145, 32, 8, COLOR_CARD_BG, COLOR_CARD_BORDER, false);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("状态", 175, 206, 2);
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
  tft.fillRect(0, 0, 320, 22, COLOR_BG);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("JK-BMS 监控", 10, 4, 2);
  drawBluetoothIcon(290, 4, bleConnected);
  tft.drawFastHLine(0, 21, 320, COLOR_CARD_BORDER);
}

void drawFooter() {
  tft.fillRect(0, 238, 320, 2, COLOR_CARD_BORDER);
}

void updateDisplay() {
  displayPower += (targetPower - displayPower) * 0.3;
  
  // 更新弹射模式
  updateBoostMode();
  
  // 清屏重绘 (弹射模式需要全屏刷新)
  if (boostIntensity > 0.05 || firstDraw) {
    tft.fillScreen(COLOR_BG);
    drawBoostBackground();
    drawSpeedLines();
    drawParticles();
    drawHeader();
    drawMainPowerDisplay();
    drawCapacityDisplay();
    drawVoltageCurrentDisplay();
    drawTemperatureDisplay();
    drawStatusDisplay();
    drawFooter();
  } else {
    if (bleConnected != lastBleState || firstDraw) {
      drawHeader();
      lastBleState = bleConnected;
    }
    drawMainPowerDisplay();
    if (firstDraw || bmsData.soc != lastSoc) {
      drawCapacityDisplay();
      drawVoltageCurrentDisplay();
      drawTemperatureDisplay();
      drawStatusDisplay();
      drawFooter();
      lastSoc = bmsData.soc;
    }
  }
  
  firstDraw = false;
  animationFrame++;
}

// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  Serial.println("JKBMS Display 启动...");
  
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);
  
  // 启动画面
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("JK-BMS", 160, 100, 4);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("弹射模式就绪...", 160, 130, 2);
  
  // 初始化弹射模式系统
  initSpeedLines();
  initParticles();
  
  NimBLEDevice::init("JKBMS-Monitor");
  
  delay(1000);
  tft.fillScreen(COLOR_BG);
  firstDraw = true;
}

// ==================== 主循环 ====================
void loop() {
  if (!bleConnected) {
    if (millis() - lastBleActivity > 10000) {
      connectToBMS();
      lastBleActivity = millis();
    }
  }
  parseJK02_32S_Frame();
  if (millis() - lastUiUpdate > 50) {
    updateDisplay();
    lastUiUpdate = millis();
  }
  delay(5);
}