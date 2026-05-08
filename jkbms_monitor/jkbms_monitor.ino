/*
 * JK BMS 蓝牙监控仪表盘 (全中文版)
 * 硬件: ESP32-32E + ST7789 2.8寸 240x320
 * BMS:  JK_BD4A24S10P (JK02_32S协议)
 * 风格: 极简电竞风 / 新能源车机仪表盘
 *
 * 需安装库:
 *   - Adafruit ST7789
 *   - Adafruit GFX Library
 *   - U8g2_for_Adafruit_GFX  (中文字体支持)
 *   - ESP32 BLE Arduino (随ESP32核心自带)
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

// ===================== 引脚配置 =====================
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1
#define TFT_SCLK  14
#define TFT_MOSI  13
#define TFT_BL    21

// ===================== BLE 配置 =====================
#define BMS_MAC  "98:DA:20:07:B9:00"
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
#define SCREEN_W 240
#define SCREEN_H 320

#define CLR_BG          0x0000
#define CLR_CYAN        0x07FF
#define CLR_GREEN       0x07E0
#define CLR_RED         0xF800
#define CLR_ORANGE      0xFD20
#define CLR_YELLOW      0xFFE0
#define CLR_WHITE       0xFFFF
#define CLR_GRAY        0x632C
#define CLR_DARK_GRAY   0x2104
#define CLR_DIM_CYAN    0x0418
#define CLR_DIM_GREEN   0x0340
#define CLR_DIM_RED     0x6000
#define CLR_BAR_BG      0x18C3
#define CLR_ACCENT      0x055F

// ===================== 显示对象 =====================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
U8g2_for_Adafruit_GFX u8g2;

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
const unsigned long DISPLAY_INTERVAL = 1500;
const unsigned long COMMAND_INTERVAL = 8000;
const unsigned long SCAN_INTERVAL = 5000;
const unsigned long DATA_TIMEOUT = 30000;

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
    Serial.println(F("[BLE] 已连接"));
  }
  void onDisconnect(BLEClient* pclient) {
    bleConnected = false;
    doConnect = false;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
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
  pClient->setConnectionParams(12, 12, 0, 150);

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

// ===================== 中文绘制辅助 =====================
void drawCN(const char* text, int x, int y, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y);
  u8g2.print(text);
}

void drawCN12(const char* text, int x, int y, uint16_t color) {
  u8g2.setFont(u8g2_font_wqy12_t_chinese3);
  drawCN(text, x, y, color);
}

void drawCN14(const char* text, int x, int y, uint16_t color) {
  u8g2.setFont(u8g2_font_wqy14_t_chinese3);
  drawCN(text, x, y, color);
}

void drawCN16(const char* text, int x, int y, uint16_t color) {
  u8g2.setFont(u8g2_font_wqy16_t_chinese3);
  drawCN(text, x, y, color);
}

// ===================== 显示辅助函数 =====================
uint16_t getSocColor(int soc) {
  if (soc > 60) return CLR_GREEN;
  if (soc > 30) return CLR_YELLOW;
  if (soc > 15) return CLR_ORANGE;
  return CLR_RED;
}

uint16_t getPowerColor() {
  if (bms.isCharging)    return CLR_GREEN;
  if (bms.isDischarging) return CLR_RED;
  return CLR_GRAY;
}

uint16_t getPowerBarColor() {
  if (bms.isCharging)    return CLR_DIM_GREEN;
  if (bms.isDischarging) return CLR_DIM_RED;
  return CLR_DARK_GRAY;
}

void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  tft.drawFastHLine(x + r, y, w - 2 * r, color);
  tft.drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  tft.drawFastVLine(x, y + r, h - 2 * r, color);
  tft.drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  tft.drawPixel(x + r - 1, y + r - 1, color);
  tft.drawPixel(x + w - r, y + r - 1, color);
  tft.drawPixel(x + r - 1, y + h - r, color);
  tft.drawPixel(x + w - r, y + h - r, color);
}

void drawProgressBar(int x, int y, int w, int h, int percent, uint16_t fillColor) {
  tft.fillRect(x, y, w, h, CLR_BAR_BG);
  int fillW = (int)((w - 4) * (float)percent / 100.0f);
  if (fillW > 0) {
    tft.fillRect(x + 2, y + 2, fillW, h - 4, fillColor);
  }
  drawRoundRect(x, y, w, h, 3, CLR_GRAY);
}

// ===================== 主界面绘制 =====================
void drawDashboard() {
  tft.fillScreen(CLR_BG);

  // ---- 顶部标题栏 ----
  drawCN12("JK BMS 监控", 8, 4, CLR_DIM_CYAN);

  tft.setTextSize(1);
  tft.setTextColor(CLR_DARK_GRAY);
  tft.setCursor(8, 20);
  tft.print(BMS_NAME);

  if (bleConnected) {
    tft.fillCircle(SCREEN_W - 10, 12, 4, CLR_GREEN);
    tft.drawCircle(SCREEN_W - 10, 12, 6, CLR_DIM_GREEN);
  } else {
    tft.fillCircle(SCREEN_W - 10, 12, 4, CLR_RED);
    tft.drawCircle(SCREEN_W - 10, 12, 6, CLR_DIM_RED);
  }

  tft.drawFastHLine(0, 32, SCREEN_W, CLR_ACCENT);

  // ---- 功率区域 (主视觉) ----
  uint16_t pwrColor = getPowerColor();
  uint16_t pwrBarColor = getPowerBarColor();

  tft.fillRect(0, 34, SCREEN_W, 4, pwrBarColor);

  drawCN12("功率", 10, 42, CLR_GRAY);

  float absPower = fabs(bms.power);
  char pwrStr[16];

  if (!bms.dataValid) {
    strcpy(pwrStr, "---");
  } else if (absPower >= 1000.0f) {
    dtostrf(absPower, 1, 0, pwrStr);
  } else if (absPower >= 100.0f) {
    dtostrf(absPower, 1, 1, pwrStr);
  } else if (absPower >= 10.0f) {
    dtostrf(absPower, 1, 1, pwrStr);
  } else {
    dtostrf(absPower, 1, 2, pwrStr);
  }

  tft.setTextSize(4);
  tft.setTextColor(pwrColor);
  tft.setCursor(10, 58);
  if (bms.isCharging && bms.dataValid) tft.print(F("+"));
  else if (bms.isDischarging && bms.dataValid) tft.print(F("-"));
  tft.print(pwrStr);

  tft.setTextSize(2);
  tft.setTextColor(pwrColor);
  int pwrLen = strlen(pwrStr);
  int unitX = 10 + (bms.isCharging || bms.isDischarging ? 24 : 0) + pwrLen * 24;
  if (unitX > SCREEN_W - 30) unitX = SCREEN_W - 30;
  tft.setCursor(unitX, 74);
  tft.print(F("W"));

  if (!bms.dataValid) {
    drawCN14("等待数据...", 10, 100, CLR_GRAY);
  } else if (bms.isCharging) {
    drawCN14("\xe2\x96\xb2 充电中", 10, 100, CLR_GREEN);
  } else if (bms.isDischarging) {
    drawCN14("\xe2\x96\xbc 放电中", 10, 100, CLR_RED);
  } else {
    drawCN14("-- 待机", 10, 100, CLR_GRAY);
  }

  tft.drawFastHLine(10, 122, SCREEN_W - 20, CLR_DARK_GRAY);

  // ---- 电池容量区域 ----
  uint16_t socColor = getSocColor(bms.soc);

  drawCN12("电池", 10, 128, CLR_GRAY);

  char socStr[8];
  if (bms.dataValid) {
    sprintf(socStr, "%d%%", bms.soc);
  } else {
    strcpy(socStr, "--%");
  }

  tft.setTextSize(5);
  tft.setTextColor(CLR_WHITE);
  tft.setCursor(10, 142);
  tft.print(socStr);

  drawProgressBar(10, 195, SCREEN_W - 20, 16, bms.dataValid ? bms.soc : 0, socColor);

  tft.setTextSize(1);
  tft.setTextColor(CLR_GRAY);
  tft.setCursor(10, 218);

  if (bms.dataValid) {
    char capStr[32];
    sprintf(capStr, "%.1f / %.1f Ah", bms.capacity_remain, bms.capacity_nominal);
    tft.print(capStr);
  } else {
    tft.print(F("--- / --- Ah"));
  }

  float usedCap = bms.capacity_nominal - bms.capacity_remain;
  if (bms.dataValid && usedCap > 0) {
    char usedStr[16];
    sprintf(usedStr, "-%.1fAh", usedCap);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(usedStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(SCREEN_W - 10 - w, 218);
    tft.setTextColor(CLR_DIM_RED);
    tft.print(usedStr);
  }

  tft.drawFastHLine(10, 234, SCREEN_W - 20, CLR_DARK_GRAY);

  // ---- 底部状态区域 ----
  drawCN12("温度", 10, 240, CLR_CYAN);

  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.setCursor(10, 256);
  if (bms.dataValid) {
    char t1Str[12];
    dtostrf(bms.temp1, 1, 1, t1Str);
    tft.print(t1Str);
    tft.setTextSize(1);
    tft.print(F(" C"));
  } else {
    tft.print(F("--"));
  }

  drawCN12("电压", 130, 240, CLR_YELLOW);

  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.setCursor(130, 256);
  if (bms.dataValid) {
    char vStr[12];
    dtostrf(bms.voltage, 1, 1, vStr);
    tft.print(vStr);
    tft.setTextSize(1);
    tft.print(F(" V"));
  } else {
    tft.print(F("--"));
  }

  drawCN12("电流", 10, 282, CLR_ORANGE);

  tft.setTextSize(2);
  tft.setTextColor(CLR_WHITE);
  tft.setCursor(10, 298);
  if (bms.dataValid) {
    char iStr[12];
    dtostrf(bms.current, 1, 2, iStr);
    tft.print(iStr);
    tft.setTextSize(1);
    tft.print(F(" A"));
  } else {
    tft.print(F("--"));
  }

  if (bms.dataValid && (millis() - bms.lastUpdate > DATA_TIMEOUT)) {
    drawCN12("超时!", 160, 298, CLR_RED);
  } else if (bleConnected) {
    drawCN12("在线", 170, 298, CLR_DIM_GREEN);
  }
}

// ===================== 启动画面 =====================
void drawSplash() {
  tft.fillScreen(CLR_BG);

  tft.drawFastHLine(40, 90, 160, CLR_ACCENT);
  tft.drawFastHLine(40, 230, 160, CLR_ACCENT);

  drawCN16("JK BMS", 55, 100, CLR_CYAN);
  drawCN14("蓝牙监控仪表盘", 35, 130, CLR_GRAY);

  tft.setTextSize(1);
  tft.setTextColor(CLR_DIM_CYAN);
  tft.setCursor(40, 160);
  tft.print(BMS_NAME);

  drawCN12("扫描中...", 70, 190, CLR_GRAY);
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

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(CLR_BG);

  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFontPosTop();

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

  delay(50);
}
