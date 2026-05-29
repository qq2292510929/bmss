/*
 * JK BMS 蓝牙监控仪表盘 - 赛车战斗模式
 * 硬件: ESP32-32E + ST7789 2.8寸 TPM408-2.8 320x240
 * BMS:  JK_BD4A24S10P (JK02_32S协议)
 * 风格: 赛车运动/战斗模式 - 全圆角、高对比度、强科技感
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2lib.h>

// ===================== 引脚配置 =====================
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1
#define TFT_SCLK  14
#define TFT_MOSI  13
#define TFT_BL    21

// ===================== BLE 配置 =====================
#define BMS_MAC  "98:da:20:07:b9:00"
#define BMS_NAME "JK_BD4A24S10P"
static BLEUUID serviceUUID((uint16_t)0xFFE0);
static BLEUUID charUUID((uint16_t)0xFFE1);
#define CMD_CELL_INFO   0x96
#define CMD_DEVICE_INFO 0x97
#define FRAME_HEADER_0 0x55
#define FRAME_HEADER_1 0xAA
#define FRAME_HEADER_2 0xEB
#define FRAME_HEADER_3 0x90
#define FRAME_TYPE_CELL_INFO 0x02
#define MIN_FRAME_SIZE 300
#define MAX_FRAME_SIZE 320

// ===================== 显示参数 =====================
#define SCREEN_W 320
#define SCREEN_H 240

// 赛车模式配色 - 高对比度科技感
#define CLR_BG          0x0A0A  // 深黑灰
#define CLR_PANEL       0x18C3  // 面板深灰
#define CLR_PANEL_HI    0x29A7  // 高亮面板
#define CLR_RACING_RED  0xD800  // 赛车红
#define CLR_RACING_RED2 0xA000  // 深赛车红
#define CLR_CYAN        0x07FF  // 亮青色
#define CLR_CYAN_DIM    0x0418  // 暗青色
#define CLR_WHITE       0xFFFF  // 纯白
#define CLR_GRAY        0x632C  // 中灰
#define CLR_DARK_GRAY   0x2104  // 深灰
#define CLR_BLACK       0x0000  // 纯黑
#define CLR_GREEN       0x07E0  // 绿
#define CLR_ORANGE      0xFD20  // 橙
#define CLR_YELLOW      0xFFE0  // 黄

// ===================== 显示对象 =====================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2CN(U8G2_R0, 33, 34, U8X8_PIN_NONE);
#define CN_BUF_MAX_PIXELS (256 * 20)
static uint16_t cnRgbBuf[CN_BUF_MAX_PIXELS];

// ===================== BMS 数据结构 =====================
struct BMSData {
  float voltage;
  float power;
  float current;
  float temp1;
  float temp2;
  int   soc;
  float capacity_remain;
  float capacity_nominal;
  bool  isCharging;
  bool  isDischarging;
  bool  dataValid;
  unsigned long lastUpdate;
};
BMSData bms;

struct PrevDisplay {
  float power;
  float voltage;
  float current;
  float temp1;
  int   soc;
  float cap_remain;
  float cap_nominal;
  bool  isCharging;
  bool  isDischarging;
  bool  bleConnected;
  bool  dataValid;
};
PrevDisplay prev;

// ===================== BLE 全局变量 =====================
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pWriteChar = nullptr;
BLERemoteCharacteristic* pNotifyChar = nullptr;
bool bleConnected = false;
bool doConnect = false;
bool doScan = false;
BLEAdvertisedDevice* advDevice = nullptr;
uint8_t frameBuf[MAX_FRAME_SIZE];
int framePos = 0;
bool frameStarted = false;
volatile bool newDataReady = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastCommandTime = 0;
unsigned long lastScanTime = 0;
const unsigned long DISPLAY_INTERVAL = 200;  // 200ms刷新，更流畅
const unsigned long COMMAND_INTERVAL = 8000;
const unsigned long SCAN_INTERVAL = 5000;
const unsigned long DATA_TIMEOUT = 30000;
bool needFullRedraw = true;

// ===================== 动画状态 =====================
unsigned long animTick = 0;
uint8_t pulsePhase = 0;
uint8_t breathePhase = 0;
float displayedPower = 0;
float displayedCurrent = 0;
float displayedVoltage = 0;
float displayedTemp = 0;
float displayedCap = 0;

// ===================== CRC 计算 =====================
uint8_t calcCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) {
    crc += data[i];
  }
  return crc;
}

// ===================== 发送BLE命令 =====================
void sendBMSCommand(uint8_t cmd) {
  if (!pWriteChar) return;
  uint8_t cmdFrame[20] = {
    0xAA, 0x55, 0x90, 0xEB,
    cmd, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
  };
  cmdFrame[19] = calcCRC(cmdFrame, 19);
  pWriteChar->writeValue(cmdFrame, 20, true);
}

// ===================== 解析JK02_32S帧 =====================
void parseCellInfoFrame() {
  if (framePos < MIN_FRAME_SIZE) return;
  uint8_t crc = calcCRC(frameBuf, framePos - 1);
  if (crc != frameBuf[framePos - 1]) {
    Serial.println(F("[BMS] CRC校验失败"));
    return;
  }
  if (frameBuf[4] != FRAME_TYPE_CELL_INFO) return;
  bms.voltage = ((uint32_t)frameBuf[121] << 24 | (uint32_t)frameBuf[120] << 16 |
                 (uint32_t)frameBuf[119] << 8  | (uint32_t)frameBuf[118]) * 0.001f;
  bms.power = ((uint32_t)frameBuf[125] << 24 | (uint32_t)frameBuf[124] << 16 |
               (uint32_t)frameBuf[123] << 8  | (uint32_t)frameBuf[122]) * 0.001f;
  int32_t rawCurrent = (int32_t)(
    (uint32_t)frameBuf[129] << 24 | (uint32_t)frameBuf[128] << 16 |
    (uint32_t)frameBuf[127] << 8  | (uint32_t)frameBuf[126]);
  bms.current = rawCurrent * 0.001f;
  int16_t rawT1 = (int16_t)((uint16_t)frameBuf[131] << 8 | frameBuf[130]);
  int16_t rawT2 = (int16_t)((uint16_t)frameBuf[133] << 8 | frameBuf[132]);
  bms.temp1 = rawT1 * 0.1f;
  bms.temp2 = rawT2 * 0.1f;
  bms.soc = frameBuf[141];
  bms.capacity_remain = ((uint32_t)frameBuf[145] << 24 | (uint32_t)frameBuf[144] << 16 |
                         (uint32_t)frameBuf[143] << 8  | (uint32_t)frameBuf[142]) * 0.001f;
  bms.capacity_nominal = ((uint32_t)frameBuf[149] << 24 | (uint32_t)frameBuf[148] << 16 |
                          (uint32_t)frameBuf[147] << 8  | (uint32_t)frameBuf[146]) * 0.001f;
  bms.isCharging    = (bms.current > 0.05f);
  bms.isDischarging = (bms.current < -0.05f);
  bms.dataValid     = true;
  bms.lastUpdate    = millis();
  newDataReady      = true;
  Serial.printf("[BMS] V=%.2f P=%.1f I=%.2f SOC=%d%% T1=%.1f Cap=%.1f/%.1f\n",
    bms.voltage, bms.power, bms.current, bms.soc, bms.temp1,
    bms.capacity_remain, bms.capacity_nominal);
}

// ===================== BLE 通知回调 =====================
void notifyCallback(BLERemoteCharacteristic* pChar,
                    uint8_t* pData, size_t length, bool isNotify) {
  if (length >= 4 &&
      pData[0] == FRAME_HEADER_0 && pData[1] == FRAME_HEADER_1 &&
      pData[2] == FRAME_HEADER_2 && pData[3] == FRAME_HEADER_3) {
    framePos = 0;
    frameStarted = true;
  }
  if (frameStarted) {
    for (size_t i = 0; i < length; i++) {
      if (framePos < MAX_FRAME_SIZE) {
        frameBuf[framePos++] = pData[i];
      }
      if (framePos >= MIN_FRAME_SIZE) {
        frameStarted = false;
        parseCellInfoFrame();
        break;
      }
    }
  }
}

// ===================== BLE 客户端回调 =====================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    bleConnected = true;
    needFullRedraw = true;
    Serial.println(F("[BLE] 已连接"));
  }
  void onDisconnect(BLEClient* pclient) {
    bleConnected = false;
    doConnect = false;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
    needFullRedraw = true;
    Serial.println(F("[BLE] 已断开"));
  }
};

// ===================== BLE 扫描回调 =====================
class MyScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().toString() == BMS_MAC) {
      Serial.println(F("[BLE] 发现目标BMS!"));
      BLEDevice::getScan()->stop();
      if (advDevice) delete advDevice;
      advDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// ===================== 连接BMS =====================
bool connectToBMS() {
  Serial.println(F("[BLE] 正在连接..."));
  if (!advDevice) return false;
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  if (!pClient->connect(advDevice)) {
    Serial.println(F("[BLE] 连接失败!"));
    delete pClient;
    pClient = nullptr;
    return false;
  }
  BLERemoteService* pService = pClient->getService(serviceUUID);
  if (!pService) {
    Serial.println(F("[BLE] 未找到服务!"));
    pClient->disconnect();
    return false;
  }
  std::map<std::string, BLERemoteCharacteristic*>* charMap = pService->getCharacteristics();
  for (auto& kv : *charMap) {
    BLERemoteCharacteristic* c = kv.second;
    if (c->canWrite() && !pWriteChar) {
      pWriteChar = c;
      Serial.println(F("[BLE] 找到写入特征"));
    }
    if (c->canNotify() && !pNotifyChar) {
      pNotifyChar = c;
      Serial.println(F("[BLE] 找到通知特征"));
    }
  }
  if (!pNotifyChar) {
    Serial.println(F("[BLE] 未找到通知特征!"));
    pClient->disconnect();
    return false;
  }
  pNotifyChar->registerForNotify(notifyCallback);
  if (!pWriteChar && pNotifyChar && pNotifyChar->canWrite()) {
    pWriteChar = pNotifyChar;
  }
  delay(500);
  sendBMSCommand(CMD_DEVICE_INFO);
  delay(500);
  sendBMSCommand(CMD_CELL_INFO);
  lastCommandTime = millis();
  return true;
}

// ===================== 中文渲染核心 =====================
void drawCN(const char* text, int x, int y, uint16_t fgColor, const uint8_t* font) {
  u8g2CN.setFont(font);
  u8g2CN.setFontMode(1);
  u8g2CN.setFontPosTop();
  int textW = u8g2CN.getUTF8Width(text);
  int textH = u8g2CN.getMaxCharHeight();
  if (textW <= 0 || textH <= 0) return;
  if (textW > 128) textW = 128;
  if (textH > 20) textH = 20;
  if (textW * textH > CN_BUF_MAX_PIXELS) return;

  u8g2CN.clearBuffer();
  u8g2CN.drawUTF8(0, 0, text);
  uint8_t* buf = u8g2CN.getBufferPtr();
  int pixelWidth = 128;

  for (int py = 0; py < textH; py++) {
    for (int px = 0; px < textW; px++) {
      int byteIdx = (py / 8) * pixelWidth + px;
      int bit = py % 8;
      bool isSet = (buf[byteIdx] >> bit) & 1;
      cnRgbBuf[py * textW + px] = isSet ? fgColor : CLR_BG;
    }
  }

  tft.drawRGBBitmap(x, y, cnRgbBuf, textW, textH);
}

void drawCN12(const char* text, int x, int y, uint16_t color) {
  drawCN(text, x, y, color, u8g2_font_wqy12_t_chinese3);
}

void drawCN14(const char* text, int x, int y, uint16_t color) {
  drawCN(text, x, y, color, u8g2_font_wqy14_t_chinese3);
}

// ===================== 赛车风格绘制函数 =====================

// 绘制圆角矩形框
void drawRoundPanel(int x, int y, int w, int h, int r, uint16_t fillColor, uint16_t borderColor) {
  tft.fillRoundRect(x, y, w, h, r, fillColor);
  tft.drawRoundRect(x, y, w, h, r, borderColor);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, borderColor);
}

// 绘制圆角卡片（底部三个数据卡片用）
void drawDataCard(int x, int y, int w, int h, int r, uint16_t accentColor, const char* label, const char* value, const char* unit) {
  // 卡片背景
  tft.fillRoundRect(x, y, w, h, r, CLR_PANEL);
  // 顶部强调条
  tft.fillRoundRect(x, y, w, 4, r, accentColor);
  tft.fillRect(x, y + 2, w, 2, accentColor);
  // 边框
  tft.drawRoundRect(x, y, w, h, r, CLR_DARK_GRAY);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, CLR_DARK_GRAY);
  // 标签（中文，最多4字）
  drawCN12(label, x + 8, y + 10, CLR_GRAY);
  // 数值
  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(value, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (w - tw) / 2, y + 32);
  tft.print(value);
  // 单位
  tft.setTextSize(1);
  tft.setTextColor(accentColor);
  tft.getTextBounds(unit, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (w - tw) / 2, y + 52);
  tft.print(unit);
}

// 渐变进度条（模拟渐变效果）
void drawGradientBar(int x, int y, int w, int h, int percent, uint16_t color1, uint16_t color2) {
  int fillW = (int)((w - 4) * (float)percent / 100.0f);
  // 背景
  tft.fillRoundRect(x, y, w, h, h / 2, CLR_DARK_GRAY);
  // 填充条（分段模拟渐变）
  if (fillW > 0) {
    int segments = 4;
    int segW = fillW / segments;
    for (int i = 0; i < segments; i++) {
      uint16_t blendColor;
      if (i < segments / 2) {
        blendColor = color1;
      } else {
        blendColor = color2;
      }
      int sx = x + 2 + i * segW;
      int sw = (i == segments - 1) ? (fillW - i * segW) : segW;
      if (sw > 0) {
        tft.fillRoundRect(sx, y + 2, sw, h - 4, (h - 4) / 2, blendColor);
      }
    }
  }
  // 高光边框
  tft.drawRoundRect(x, y, w, h, h / 2, CLR_GRAY);
}

// 脉冲动画强度计算
uint8_t getPulseIntensity() {
  return (sin((millis() % 1000) / 1000.0 * 2 * PI) + 1.0) * 127;
}

// 呼吸灯颜色
uint16_t getBreatheColor(uint16_t baseColor, uint8_t phase) {
  // 简化呼吸效果：通过亮度调整
  return baseColor;
}

// 数字过渡插值
float lerpFloat(float current, float target, float factor) {
  return current + (target - current) * factor;
}

// ===================== 变化检测 =====================
bool dataChanged() {
  bool changed = false;
  if (bms.power != prev.power) changed = true;
  if (bms.voltage != prev.voltage) changed = true;
  if (bms.current != prev.current) changed = true;
  if (bms.temp1 != prev.temp1) changed = true;
  if (bms.soc != prev.soc) changed = true;
  if (bms.capacity_remain != prev.cap_remain) changed = true;
  if (bms.capacity_nominal != prev.cap_nominal) changed = true;
  if (bms.isCharging != prev.isCharging) changed = true;
  if (bms.isDischarging != prev.isDischarging) changed = true;
  if (bms.dataValid != prev.dataValid) changed = true;
  if (bleConnected != prev.bleConnected) changed = true;
  return changed;
}

void savePrevData() {
  prev.power = bms.power;
  prev.voltage = bms.voltage;
  prev.current = bms.current;
  prev.temp1 = bms.temp1;
  prev.soc = bms.soc;
  prev.cap_remain = bms.capacity_remain;
  prev.cap_nominal = bms.capacity_nominal;
  prev.isCharging = bms.isCharging;
  prev.isDischarging = bms.isDischarging;
  prev.dataValid = bms.dataValid;
  prev.bleConnected = bleConnected;
}

// ===================== 绘制静态元素（赛车风格） =====================
void drawStaticElements() {
  // 顶部装饰条
  tft.fillRect(0, 0, SCREEN_W, 3, CLR_RACING_RED);
  tft.fillRect(0, 3, SCREEN_W, 1, CLR_RACING_RED2);

  // 顶部标题区背景
  tft.fillRoundRect(8, 8, 160, 22, 4, CLR_PANEL);
  tft.drawRoundRect(8, 8, 160, 22, 4, CLR_DARK_GRAY);

  // BLE状态框
  tft.fillRoundRect(SCREEN_W - 50, 8, 42, 22, 4, CLR_PANEL);
  tft.drawRoundRect(SCREEN_W - 50, 8, 42, 22, 4, CLR_DARK_GRAY);

  // 主功率区大面板
  drawRoundPanel(8, 36, 304, 110, 8, CLR_PANEL, CLR_DARK_GRAY);

  // 容量区面板
  drawRoundPanel(8, 152, 200, 36, 6, CLR_PANEL, CLR_DARK_GRAY);

  // 底部三个数据卡片背景（静态部分）
  // 温度卡片
  drawRoundPanel(8, 196, 96, 40, 6, CLR_PANEL, CLR_DARK_GRAY);
  // 电压卡片
  drawRoundPanel(112, 196, 96, 40, 6, CLR_PANEL, CLR_DARK_GRAY);
  // 电流卡片
  drawRoundPanel(216, 196, 96, 40, 6, CLR_PANEL, CLR_DARK_GRAY);
}

// ===================== 绘制动态元素（赛车风格） =====================
void drawDynamicElements() {
  // ---- 动画更新 ----
  animTick = millis();
  pulsePhase = (animTick / 50) % 20;
  breathePhase = (animTick / 30) % 256;

  // 数字平滑过渡
  float lerpFactor = 0.3f;
  if (bms.dataValid) {
    displayedPower = lerpFloat(displayedPower, fabs(bms.power), lerpFactor);
    displayedCurrent = lerpFloat(displayedCurrent, bms.current, lerpFactor);
    displayedVoltage = lerpFloat(displayedVoltage, bms.voltage, lerpFactor);
    displayedTemp = lerpFloat(displayedTemp, bms.temp1, lerpFactor);
    displayedCap = lerpFloat(displayedCap, bms.capacity_remain, lerpFactor);
  }

  // ---- 标题栏 ----
  tft.fillRect(12, 10, 152, 18, CLR_PANEL);
  drawCN12("BMS监控", 14, 12, CLR_CYAN);
  tft.setTextSize(1);
  tft.setTextColor(CLR_GRAY);
  tft.setCursor(80, 14);
  tft.print(BMS_NAME);

  // BLE状态呼吸灯
  uint16_t bleColor;
  if (bleConnected) {
    // 呼吸效果：绿色脉冲
    int breathe = (sin((millis() % 1500) / 1500.0 * 2 * PI) + 1.0) * 50;
    bleColor = tft.color565(0, 150 + breathe, 0);
    tft.fillRoundRect(SCREEN_W - 48, 10, 38, 18, 3, tft.color565(0, 40, 0));
    drawCN12("在线", SCREEN_W - 42, 13, bleColor);
  } else {
    tft.fillRoundRect(SCREEN_W - 48, 10, 38, 18, 3, tft.color565(60, 0, 0));
    drawCN12("断开", SCREEN_W - 42, 13, CLR_RACING_RED);
  }

  // ---- 主功率显示区（居中大字） ----
  uint16_t pwrColor = bms.isCharging ? CLR_CYAN : (bms.isDischarging ? CLR_RACING_RED : CLR_GRAY);
  uint16_t pulseColor = bms.isCharging ? CLR_CYAN_DIM : CLR_RACING_RED2;

  // 充放电脉冲动画条
  if (bms.dataValid && (bms.isCharging || bms.isDischarging)) {
    int pulseW = 20 + pulsePhase * 8;
    int pulseX = 16 + (pulsePhase % 3) * 80;
    if (pulseX + pulseW < 304) {
      tft.fillRoundRect(pulseX, 38, pulseW, 2, 1, pulseColor);
    }
  }

  // 功率标签
  drawCN12("功率", 20, 42, CLR_GRAY);

  // 功率大数字
  char pwrStr[16];
  if (!bms.dataValid) {
    strcpy(pwrStr, "---");
  } else if (displayedPower >= 1000.0f) {
    dtostrf(displayedPower, 1, 0, pwrStr);
  } else if (displayedPower >= 100.0f) {
    dtostrf(displayedPower, 1, 1, pwrStr);
  } else {
    dtostrf(displayedPower, 1, 2, pwrStr);
  }

  // 清除功率显示区域
  tft.fillRect(16, 58, 288, 60, CLR_PANEL);

  // 功率符号
  tft.setTextSize(1);
  tft.setTextColor(pwrColor);
  if (bms.dataValid) {
    if (bms.isCharging) {
      tft.setCursor(60, 65);
      tft.print(F("+"));
    } else if (bms.isDischarging) {
      tft.setCursor(60, 65);
      tft.print(F("-"));
    }
  }

  // 功率数值（超大字）
  tft.setTextSize(6);
  tft.setTextColor(pwrColor);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(pwrStr, 0, 0, &x1, &y1, &tw, &th);
  int pwrX = 160 - tw / 2 + 10;
  tft.setCursor(pwrX, 62);
  tft.print(pwrStr);

  // 单位 W
  tft.setTextSize(2);
  tft.setTextColor(pwrColor);
  tft.setCursor(pwrX + tw + 8, 82);
  tft.print(F("W"));

  // 状态标签（充放电/待机）
  tft.fillRect(100, 118, 120, 20, CLR_PANEL);
  if (!bms.dataValid) {
    drawCN14("待连接", 130, 118, CLR_GRAY);
  } else if (bms.isCharging) {
    // 脉冲动画颜色
    int pulseBright = 100 + (pulsePhase * 8);
    if (pulseBright > 255) pulseBright = 255;
    uint16_t chargePulse = tft.color565(0, pulseBright, pulseBright);
    drawCN14("充电中", 130, 118, chargePulse);
  } else if (bms.isDischarging) {
    int pulseBright = 100 + (pulsePhase * 8);
    if (pulseBright > 255) pulseBright = 255;
    uint16_t dischargePulse = tft.color565(pulseBright, 0, 0);
    drawCN14("放电中", 130, 118, dischargePulse);
  } else {
    drawCN14("待机", 140, 118, CLR_GRAY);
  }

  // ---- 容量区 ----
  uint16_t socColor = bms.soc > 60 ? CLR_CYAN : (bms.soc > 30 ? CLR_YELLOW : (bms.soc > 15 ? CLR_ORANGE : CLR_RACING_RED));

  // SOC大数字
  char socStr[8];
  if (bms.dataValid) {
    sprintf(socStr, "%d%%", bms.soc);
  } else {
    strcpy(socStr, "--%");
  }

  tft.fillRect(16, 156, 80, 28, CLR_PANEL);
  tft.setTextSize(3);
  tft.setTextColor(socColor);
  tft.setCursor(20, 158);
  tft.print(socStr);

  // 容量进度条
  int socPercent = bms.dataValid ? bms.soc : 0;
  drawGradientBar(100, 160, 100, 16, socPercent, socColor, CLR_CYAN_DIM);

  // 容量数值
  tft.fillRect(210, 156, 90, 28, CLR_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(CLR_GRAY);
  tft.setCursor(212, 158);
  if (bms.dataValid) {
    char capStr[32];
    sprintf(capStr, "%.1f/%.1fAh", bms.capacity_remain, bms.capacity_nominal);
    tft.print(capStr);
  } else {
    tft.print(F("---/---Ah"));
  }

  // ---- 底部三个圆角数据卡片 ----
  char valStr[16];

  // 温度卡片 - 青色强调
  if (bms.dataValid) {
    dtostrf(displayedTemp, 1, 1, valStr);
  } else {
    strcpy(valStr, "--");
  }
  // 清除并重绘卡片内容
  tft.fillRoundRect(10, 198, 92, 36, 4, CLR_PANEL);
  tft.fillRoundRect(10, 198, 92, 4, 4, CLR_CYAN);
  tft.fillRect(10, 200, 92, 2, CLR_CYAN);
  drawCN12("温度", 16, 202, CLR_GRAY);
  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.getTextBounds(valStr, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(10 + (92 - tw) / 2, 218);
  tft.print(valStr);
  tft.setTextSize(1);
  tft.setTextColor(CLR_CYAN);
  tft.print(F(" C"));

  // 电压卡片 - 黄色强调
  if (bms.dataValid) {
    dtostrf(displayedVoltage, 1, 1, valStr);
  } else {
    strcpy(valStr, "--");
  }
  tft.fillRoundRect(114, 198, 92, 36, 4, CLR_PANEL);
  tft.fillRoundRect(114, 198, 92, 4, 4, CLR_YELLOW);
  tft.fillRect(114, 200, 92, 2, CLR_YELLOW);
  drawCN12("电压", 120, 202, CLR_GRAY);
  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.getTextBounds(valStr, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(114 + (92 - tw) / 2, 218);
  tft.print(valStr);
  tft.setTextSize(1);
  tft.setTextColor(CLR_YELLOW);
  tft.print(F(" V"));

  // 电流卡片 - 赛车红强调
  if (bms.dataValid) {
    dtostrf(displayedCurrent, 1, 2, valStr);
  } else {
    strcpy(valStr, "--");
  }
  tft.fillRoundRect(218, 198, 92, 36, 4, CLR_PANEL);
  tft.fillRoundRect(218, 198, 92, 4, 4, CLR_RACING_RED);
  tft.fillRect(218, 200, 92, 2, CLR_RACING_RED);
  drawCN12("电流", 224, 202, CLR_GRAY);
  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.getTextBounds(valStr, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(218 + (92 - tw) / 2, 218);
  tft.print(valStr);
  tft.setTextSize(1);
  tft.setTextColor(CLR_RACING_RED);
  tft.print(F(" A"));

  // 底部装饰线
  tft.fillRect(0, SCREEN_H - 2, SCREEN_W, 2, CLR_RACING_RED);
}

// ===================== 主界面绘制 =====================
void drawDashboard() {
  if (needFullRedraw) {
    tft.fillScreen(CLR_BG);
    drawStaticElements();
    drawDynamicElements();
    needFullRedraw = false;
    savePrevData();
    return;
  }
  if (!dataChanged() && !newDataReady) return;
  newDataReady = false;
  drawDynamicElements();
  savePrevData();
}

// ===================== 启动画面（赛车风格） =====================
void drawSplash() {
  tft.fillScreen(CLR_BG);

  // 顶部红线
  tft.fillRect(0, 0, SCREEN_W, 4, CLR_RACING_RED);

  // 中央大面板
  drawRoundPanel(20, 50, 280, 140, 12, CLR_PANEL, CLR_RACING_RED);

  // 标题
  drawCN14("JK BMS", 120, 65, CLR_CYAN);
  drawCN12("监控", 140, 90, CLR_GRAY);

  // BMS名称
  tft.setTextSize(1);
  tft.setTextColor(CLR_RACING_RED);
  tft.setCursor(110, 115);
  tft.print(BMS_NAME);

  // 扫描动画点
  int dotPhase = (millis() / 300) % 4;
  tft.setTextColor(CLR_CYAN);
  tft.setCursor(130, 140);
  tft.print(F("连接中"));
  for (int i = 0; i < dotPhase; i++) {
    tft.print(F("."));
  }

  // 底部红线
  tft.fillRect(0, SCREEN_H - 4, SCREEN_W, 4, CLR_RACING_RED);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println(F("[系统] 启动中..."));

  bms.dataValid = false;
  bms.soc = 0;
  bms.voltage = 0;
  bms.power = 0;
  bms.current = 0;
  bms.temp1 = 0;
  bms.temp2 = 0;
  bms.capacity_remain = 0;
  bms.capacity_nominal = 0;
  bms.isCharging = false;
  bms.isDischarging = false;
  bms.lastUpdate = 0;

  displayedPower = 0;
  displayedCurrent = 0;
  displayedVoltage = 0;
  displayedTemp = 0;
  displayedCap = 0;

  memset(&prev, 0xFF, sizeof(prev));
  prev.bleConnected = !bleConnected;
  needFullRedraw = true;

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(3);
  tft.invertDisplay(false);
  tft.fillScreen(CLR_BG);

  u8g2CN.setFont(u8g2_font_wqy12_t_chinese3);
  u8g2CN.setFontMode(1);
  u8g2CN.setFontPosTop();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  drawSplash();
  delay(1000);

  BLEDevice::init("");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyScanCallback());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(30, false);
  Serial.println(F("[系统] BLE扫描已启动"));
}

// ===================== LOOP =====================
void loop() {
  if (doConnect) {
    doConnect = false;
    if (connectToBMS()) {
      Serial.println(F("[系统] BMS连接成功!"));
    } else {
      Serial.println(F("[系统] BMS连接失败，将重试..."));
      doScan = true;
    }
  }

  if (bleConnected && (millis() - lastCommandTime > COMMAND_INTERVAL)) {
    sendBMSCommand(CMD_CELL_INFO);
    lastCommandTime = millis();
  }

  if (millis() - lastDisplayUpdate > DISPLAY_INTERVAL) {
    drawDashboard();
    lastDisplayUpdate = millis();
  }

  if (!bleConnected && !doConnect) {
    if (millis() - lastScanTime > SCAN_INTERVAL) {
      Serial.println(F("[系统] 重新扫描..."));
      BLEScan* pScan = BLEDevice::getScan();
      pScan->start(10, false);
      lastScanTime = millis();
    }
  }

  delay(20);  // 20ms延迟，更流畅的动画
}
