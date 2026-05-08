/*
 * JK BMS 蓝牙监控仪表盘 (全中文 + 无闪烁 + 电竞风UI)
 * 硬件: ESP32-32E + ST7789 2.8寸 240x320
 * BMS:  JK_BD4A24S10P (JK02_32S协议)
 *
 * 需安装库:
 *   - Adafruit ST7789
 *   - Adafruit GFX Library
 *   - U8g2  (中文字体渲染)
 *   - ESP32 BLE Arduino (随ESP32核心自带)
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
#define SW 240
#define SH 320

#define C_BG          0x0000
#define C_CYAN        0x07FF
#define C_GREEN       0x07E0
#define C_RED         0xF800
#define C_ORANGE      0xFD20
#define C_YELLOW      0xFFE0
#define C_WHITE       0xFFFF
#define C_GRAY        0x632C
#define C_DGRAY       0x2104
#define C_DCYAN       0x0418
#define C_DGREEN      0x0340
#define C_DRED        0x6000
#define C_BAR_BG      0x18C3
#define C_ACCENT      0x055F
#define C_CARD        0x1082
#define C_GLOW_G      0x01E0
#define C_GLOW_R      0x4800
#define C_GLOW_Y      0x7BE0

// ===================== 布局常量 =====================
#define HDR_Y       0
#define HDR_H       34
#define PWR_Y       36
#define PWR_H       100
#define BAT_Y       140
#define BAT_H       108
#define BOT_Y       252
#define BOT_H       66

// ===================== 显示对象 =====================
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2CN(U8G2_R0, 33, 34, U8X8_PIN_NONE);

#define CN_BUF_MAX_PIXELS (128 * 28)
static uint16_t cnRgbBuf[CN_BUF_MAX_PIXELS];

// ===================== BMS 数据 =====================
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
  float power; float voltage; float current; float temp1;
  int soc; float cap_remain; float cap_nominal;
  bool isCharging; bool isDischarging; bool dataValid; bool bleConnected;
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
const unsigned long DISPLAY_INTERVAL = 500;
const unsigned long COMMAND_INTERVAL = 8000;
const unsigned long SCAN_INTERVAL = 5000;
const unsigned long DATA_TIMEOUT = 30000;

bool needFullRedraw = true;

// ===================== CRC =====================
uint8_t calcCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) crc += data[i];
  return crc;
}

// ===================== BLE 命令 =====================
void sendBMSCommand(uint8_t cmd) {
  if (!pWriteChar) return;
  uint8_t f[20] = {0xAA,0x55,0x90,0xEB, cmd,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0};
  f[19] = calcCRC(f, 19);
  pWriteChar->writeValue(f, 20, true);
}

// ===================== 解析JK02_32S帧 =====================
void parseCellInfoFrame() {
  if (framePos < MIN_FRAME_SIZE) return;
  if (calcCRC(frameBuf, framePos - 1) != frameBuf[framePos - 1]) return;
  if (frameBuf[4] != FRAME_TYPE_CELL_INFO) return;

  bms.voltage = ((uint32_t)frameBuf[121]<<24|(uint32_t)frameBuf[120]<<16|
                 (uint32_t)frameBuf[119]<<8|(uint32_t)frameBuf[118]) * 0.001f;
  bms.power = ((uint32_t)frameBuf[125]<<24|(uint32_t)frameBuf[124]<<16|
               (uint32_t)frameBuf[123]<<8|(uint32_t)frameBuf[122]) * 0.001f;
  int32_t rawI = (int32_t)((uint32_t)frameBuf[129]<<24|(uint32_t)frameBuf[128]<<16|
               (uint32_t)frameBuf[127]<<8|(uint32_t)frameBuf[126]);
  bms.current = rawI * 0.001f;
  int16_t rT1 = (int16_t)((uint16_t)frameBuf[131]<<8|frameBuf[130]);
  bms.temp1 = rT1 * 0.1f;
  int16_t rT2 = (int16_t)((uint16_t)frameBuf[133]<<8|frameBuf[132]);
  bms.temp2 = rT2 * 0.1f;
  bms.soc = frameBuf[141];
  bms.capacity_remain = ((uint32_t)frameBuf[145]<<24|(uint32_t)frameBuf[144]<<16|
                         (uint32_t)frameBuf[143]<<8|(uint32_t)frameBuf[142]) * 0.001f;
  bms.capacity_nominal = ((uint32_t)frameBuf[149]<<24|(uint32_t)frameBuf[148]<<16|
                          (uint32_t)frameBuf[147]<<8|(uint32_t)frameBuf[146]) * 0.001f;
  bms.isCharging    = (bms.current > 0.05f);
  bms.isDischarging = (bms.current < -0.05f);
  bms.dataValid     = true;
  bms.lastUpdate    = millis();
  newDataReady      = true;

  Serial.printf("[BMS] V=%.2f P=%.1f I=%.2f SOC=%d%% T=%.1f Cap=%.1f/%.1f\n",
    bms.voltage, bms.power, bms.current, bms.soc, bms.temp1,
    bms.capacity_remain, bms.capacity_nominal);
}

// ===================== BLE 回调 =====================
void notifyCallback(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
  if (length >= 4 && pData[0]==0x55 && pData[1]==0xAA && pData[2]==0xEB && pData[3]==0x90) {
    framePos = 0;
    frameStarted = true;
  }
  if (frameStarted) {
    for (size_t i = 0; i < length; i++) {
      if (framePos < MAX_FRAME_SIZE) frameBuf[framePos++] = pData[i];
      if (framePos >= MIN_FRAME_SIZE) {
        frameStarted = false;
        parseCellInfoFrame();
        break;
      }
    }
  }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient*) {
    bleConnected = true;
    needFullRedraw = true;
    Serial.println(F("[BLE] 已连接"));
  }
  void onDisconnect(BLEClient*) {
    bleConnected = false;
    doConnect = false;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
    needFullRedraw = true;
    Serial.println(F("[BLE] 已断开"));
  }
};

class MyScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.getAddress().toString() == BMS_MAC) {
      Serial.println(F("[BLE] 发现目标BMS!"));
      BLEDevice::getScan()->stop();
      if (advDevice) delete advDevice;
      advDevice = new BLEAdvertisedDevice(dev);
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
    delete pClient; pClient = nullptr;
    return false;
  }

  BLERemoteService* pSvc = pClient->getService(serviceUUID);
  if (!pSvc) { pClient->disconnect(); return false; }

  auto* charMap = pSvc->getCharacteristics();
  for (auto& kv : *charMap) {
    BLERemoteCharacteristic* c = kv.second;
    if (c->canWrite() && !pWriteChar) pWriteChar = c;
    if (c->canNotify() && !pNotifyChar) pNotifyChar = c;
  }

  if (!pNotifyChar) { pClient->disconnect(); return false; }

  pNotifyChar->registerForNotify(notifyCallback);
  if (!pWriteChar && pNotifyChar && pNotifyChar->canWrite()) pWriteChar = pNotifyChar;

  delay(500);
  sendBMSCommand(CMD_DEVICE_INFO);
  delay(500);
  sendBMSCommand(CMD_CELL_INFO);
  lastCommandTime = millis();
  return true;
}

// ===================== 中文渲染核心(修复裁切) =====================
void drawCN(const char* text, int x, int y, uint16_t fgColor, const uint8_t* font) {
  u8g2CN.setFont(font);
  u8g2CN.setFontMode(1);
  u8g2CN.setFontPosBaseline();

  int textW = u8g2CN.getUTF8Width(text);
  int ascent = u8g2CN.getAscent();
  int descent = u8g2CN.getDescent();
  int textH = ascent - descent;

  if (textW <= 0 || textH <= 0) return;
  if (textW > 128) textW = 128;
  if (textH > 28) textH = 28;
  if (textW * textH > CN_BUF_MAX_PIXELS) return;

  u8g2CN.clearBuffer();
  u8g2CN.drawUTF8(0, ascent, text);

  uint8_t* buf = u8g2CN.getBufferPtr();
  int pw = (int)u8g2CN.getBufferTileWidth() * 8;

  for (int py = 0; py < textH; py++) {
    for (int px = 0; px < textW; px++) {
      int byteIdx = (py / 8) * pw + px;
      int bit = py % 8;
      bool isSet = false;
      if (byteIdx >= 0 && byteIdx < 1024) {
        isSet = (buf[byteIdx] >> bit) & 1;
      }
      cnRgbBuf[py * textW + px] = isSet ? fgColor : C_BG;
    }
  }

  tft.drawRGBBitmap(x, y, cnRgbBuf, textW, textH);
}

void drawCN12(const char* t, int x, int y, uint16_t c) {
  drawCN(t, x, y, c, u8g2_font_wqy12_t_chinese3);
}
void drawCN14(const char* t, int x, int y, uint16_t c) {
  drawCN(t, x, y, c, u8g2_font_wqy14_t_chinese3);
}
void drawCN16(const char* t, int x, int y, uint16_t c) {
  drawCN(t, x, y, c, u8g2_font_wqy16_t_chinese3);
}

// ===================== 显示辅助 =====================
uint16_t socColor(int s) {
  if (s > 60) return C_GREEN;
  if (s > 30) return C_YELLOW;
  if (s > 15) return C_ORANGE;
  return C_RED;
}

uint16_t socGlow(int s) {
  if (s > 60) return C_GLOW_G;
  if (s > 30) return C_GLOW_Y;
  if (s > 15) return C_ORANGE;
  return C_GLOW_R;
}

uint16_t pwrColor() {
  if (bms.isCharging) return C_GREEN;
  if (bms.isDischarging) return C_RED;
  return C_GRAY;
}

uint16_t pwrGlow() {
  if (bms.isCharging) return C_GLOW_G;
  if (bms.isDischarging) return C_GLOW_R;
  return C_DGRAY;
}

void drawCornerMarks(int x, int y, int w, int h, uint16_t c) {
  int cl = 6;
  tft.drawFastHLine(x, y, cl, c);
  tft.drawFastVLine(x, y, cl, c);
  tft.drawFastHLine(x + w - cl, y, cl, c);
  tft.drawFastVLine(x + w - 1, y, cl, c);
  tft.drawFastHLine(x, y + h - 1, cl, c);
  tft.drawFastVLine(x, y + h - cl, cl, c);
  tft.drawFastHLine(x + w - cl, y + h - 1, cl, c);
  tft.drawFastVLine(x + w - 1, y + h - cl, cl, c);
}

void drawGlowBar(int x, int y, int w, int h, int pct, uint16_t fillC, uint16_t glowC) {
  tft.fillRect(x, y, w, h, C_BAR_BG);
  int fw = (int)((w - 4) * (float)constrain(pct, 0, 100) / 100.0f);
  if (fw > 0) {
    tft.fillRect(x + 2, y, fw, h, glowC);
    tft.fillRect(x + 2, y + 2, fw, h - 4, fillC);
  }
  tft.drawFastHLine(x, y, w, C_DGRAY);
  tft.drawFastHLine(x, y + h - 1, w, C_DGRAY);
}

void drawAccentLine(int y, uint16_t c) {
  tft.drawFastHLine(0, y, SW, c);
  tft.drawFastHLine(0, y + 1, SW, C_BG);
}

// ===================== 变化检测 =====================
bool dataChanged() {
  return bms.power != prev.power || bms.voltage != prev.voltage ||
         bms.current != prev.current || bms.temp1 != prev.temp1 ||
         bms.soc != prev.soc || bms.capacity_remain != prev.cap_remain ||
         bms.isCharging != prev.isCharging || bms.isDischarging != prev.isDischarging ||
         bms.dataValid != prev.dataValid || bleConnected != prev.bleConnected;
}

void savePrev() {
  prev.power = bms.power; prev.voltage = bms.voltage;
  prev.current = bms.current; prev.temp1 = bms.temp1;
  prev.soc = bms.soc; prev.cap_remain = bms.capacity_remain;
  prev.isCharging = bms.isCharging; prev.isDischarging = bms.isDischarging;
  prev.dataValid = bms.dataValid; prev.bleConnected = bleConnected;
}

// ===================== 静态元素 =====================
void drawStatic() {
  drawAccentLine(HDR_H, C_ACCENT);

  drawCornerMarks(2, PWR_Y, SW - 4, PWR_H, C_DGRAY);
  tft.drawFastVLine(6, PWR_Y + 2, 2, pwrGlow());

  drawCornerMarks(2, BAT_Y, SW - 4, BAT_H, C_DGRAY);
  tft.drawFastVLine(6, BAT_Y + 2, 2, socGlow(bms.soc));

  drawCornerMarks(2, BOT_Y, SW - 4, BOT_H, C_DGRAY);

  drawCN12("功率", 10, PWR_Y + 6, C_GRAY);
  drawCN12("电池", 10, BAT_Y + 6, C_GRAY);

  tft.drawFastVLine(78, BOT_Y + 6, BOT_H - 12, C_DGRAY);
  tft.drawFastVLine(158, BOT_Y + 6, BOT_H - 12, C_DGRAY);

  drawCN12("温度", 14, BOT_Y + 6, C_CYAN);
  drawCN12("电压", 90, BOT_Y + 6, C_YELLOW);
  drawCN12("电流", 168, BOT_Y + 6, C_ORANGE);
}

// ===================== 动态元素 =====================
void drawDynamic() {
  // ---- 标题栏 ----
  tft.fillRect(8, 4, 170, 14, C_BG);
  drawCN14("JK BMS 监控", 8, 4, C_DCYAN);

  tft.fillRect(8, 20, 170, 10, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_DGRAY);
  tft.setCursor(8, 20);
  tft.print(BMS_NAME);

  float pulse = (sin(millis() / 600.0 * PI) + 1.0) * 0.5;
  if (bleConnected) {
    int r = 4 + (int)(pulse * 2);
    tft.fillCircle(SW - 12, 14, 4, C_GREEN);
    tft.drawCircle(SW - 12, 14, r, C_DGREEN);
  } else {
    int r = 4 + (int)(pulse * 2);
    tft.fillCircle(SW - 12, 14, 4, C_RED);
    tft.drawCircle(SW - 12, 14, r, C_DRED);
  }

  // ---- 功率区域 ----
  uint16_t pc = pwrColor();
  uint16_t pg = pwrGlow();

  tft.fillRect(0, PWR_Y, SW, 2, pg);

  float absP = fabs(bms.power);
  char ps[16];
  if (!bms.dataValid) strcpy(ps, "---");
  else if (absP >= 1000) dtostrf(absP, 1, 0, ps);
  else if (absP >= 100) dtostrf(absP, 1, 1, ps);
  else dtostrf(absP, 1, 2, ps);

  tft.fillRect(10, PWR_Y + 20, 220, 44, C_BG);
  tft.setTextSize(4);
  tft.setTextColor(pc);
  tft.setCursor(10, PWR_Y + 22);
  if (bms.isCharging && bms.dataValid) tft.print(F("+"));
  else if (bms.isDischarging && bms.dataValid) tft.print(F("-"));
  tft.print(ps);

  tft.setTextSize(2);
  tft.setTextColor(pc);
  int uw = 10 + (bms.isCharging || bms.isDischarging ? 24 : 0) + strlen(ps) * 24;
  if (uw > SW - 30) uw = SW - 30;
  tft.setCursor(uw, PWR_Y + 38);
  tft.print(F("W"));

  tft.fillRect(10, PWR_Y + 66, 180, 18, C_BG);
  if (!bms.dataValid) {
    drawCN14("等待数据...", 10, PWR_Y + 66, C_GRAY);
  } else if (bms.isCharging) {
    drawCN14("\xe2\x96\xb2 充电中", 10, PWR_Y + 66, C_GREEN);
  } else if (bms.isDischarging) {
    drawCN14("\xe2\x96\xbc 放电中", 10, PWR_Y + 66, C_RED);
  } else {
    drawCN14("-- 待机", 10, PWR_Y + 66, C_GRAY);
  }

  int pwrPct = bms.dataValid ? constrain((int)(absP / 30.0 * 100), 0, 100) : 0;
  drawGlowBar(10, PWR_Y + 86, SW - 20, 8, pwrPct, pc, pg);

  // ---- 电池区域 ----
  uint16_t sc = socColor(bms.soc);
  uint16_t sg = socGlow(bms.soc);

  char ss[8];
  if (bms.dataValid) sprintf(ss, "%d%%", bms.soc);
  else strcpy(ss, "--%");

  tft.fillRect(10, BAT_Y + 20, 200, 44, C_BG);
  tft.setTextSize(4);
  tft.setTextColor(C_WHITE);
  tft.setCursor(10, BAT_Y + 22);
  tft.print(ss);

  tft.setTextSize(2);
  tft.setTextColor(sc);
  tft.setCursor(10 + strlen(ss) * 24, BAT_Y + 38);
  tft.print(F(" "));

  drawGlowBar(10, BAT_Y + 68, SW - 20, 16, bms.dataValid ? bms.soc : 0, sc, sg);

  tft.fillRect(10, BAT_Y + 88, 220, 14, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_GRAY);
  tft.setCursor(10, BAT_Y + 90);
  if (bms.dataValid) {
    char cs[32];
    sprintf(cs, "%.1f / %.1f Ah", bms.capacity_remain, bms.capacity_nominal);
    tft.print(cs);
  } else {
    tft.print(F("--- / --- Ah"));
  }

  float used = bms.capacity_nominal - bms.capacity_remain;
  if (bms.dataValid && used > 0) {
    char us[16];
    sprintf(us, "-%.1fAh", used);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(us, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(SW - 10 - w, BAT_Y + 90);
    tft.setTextColor(C_DRED);
    tft.print(us);
  }

  // ---- 底部区域 ----
  tft.fillRect(8, BOT_Y + 22, 66, 38, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(10, BOT_Y + 26);
  if (bms.dataValid) {
    char ts[10];
    dtostrf(bms.temp1, 1, 1, ts);
    tft.print(ts);
    tft.setTextSize(1);
    tft.print(F("C"));
  } else tft.print(F("--"));

  tft.fillRect(84, BOT_Y + 22, 66, 38, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(86, BOT_Y + 26);
  if (bms.dataValid) {
    char vs[10];
    dtostrf(bms.voltage, 1, 1, vs);
    tft.print(vs);
    tft.setTextSize(1);
    tft.print(F("V"));
  } else tft.print(F("--"));

  tft.fillRect(162, BOT_Y + 22, 72, 38, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(164, BOT_Y + 26);
  if (bms.dataValid) {
    char is[10];
    dtostrf(bms.current, 1, 2, is);
    tft.print(is);
    tft.setTextSize(1);
    tft.print(F("A"));
  } else tft.print(F("--"));

  tft.fillRect(162, BOT_Y + 50, 72, 12, C_BG);
  if (bms.dataValid && (millis() - bms.lastUpdate > DATA_TIMEOUT)) {
    drawCN12("超时!", 180, BOT_Y + 50, C_RED);
  } else if (bleConnected) {
    drawCN12("在线", 186, BOT_Y + 50, C_DGREEN);
  }
}

// ===================== 主绘制 =====================
void drawDashboard() {
  if (needFullRedraw) {
    tft.fillScreen(C_BG);
    drawStatic();
    drawDynamic();
    needFullRedraw = false;
    savePrev();
    return;
  }
  if (!dataChanged() && !newDataReady) return;
  newDataReady = false;
  drawDynamic();
  savePrev();
}

// ===================== 启动画面 =====================
void drawSplash() {
  tft.fillScreen(C_BG);

  for (int i = 0; i < 3; i++) {
    int y = 80 + i * 4;
    tft.drawFastHLine(30 + i * 8, y, SW - 60 - i * 16, C_ACCENT);
  }
  for (int i = 0; i < 3; i++) {
    int y = 220 + i * 4;
    tft.drawFastHLine(30 + i * 8, y, SW - 60 - i * 16, C_ACCENT);
  }

  drawCN16("JK BMS", 60, 96, C_CYAN);
  drawCN14("蓝牙监控仪表盘", 35, 126, C_GRAY);

  tft.setTextSize(1);
  tft.setTextColor(C_DCYAN);
  tft.setCursor(50, 156);
  tft.print(BMS_NAME);

  drawCN12("扫描中...", 76, 186, C_GRAY);

  drawCornerMarks(20, 74, SW - 40, 160, C_DGRAY);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println(F("[系统] 启动中..."));

  bms = {0};
  bms.dataValid = false;
  memset(&prev, 0xFF, sizeof(prev));
  prev.bleConnected = !bleConnected;
  needFullRedraw = true;

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(SW, SH);
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(C_BG);

  u8g2CN.setFont(u8g2_font_wqy12_t_chinese3);
  u8g2CN.setFontMode(1);
  u8g2CN.setFontPosBaseline();

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
    if (connectToBMS()) Serial.println(F("[系统] BMS连接成功!"));
    else { Serial.println(F("[系统] BMS连接失败")); doScan = true; }
  }

  if (bleConnected && millis() - lastCommandTime > COMMAND_INTERVAL) {
    sendBMSCommand(CMD_CELL_INFO);
    lastCommandTime = millis();
  }

  if (millis() - lastDisplayUpdate > DISPLAY_INTERVAL) {
    drawDashboard();
    lastDisplayUpdate = millis();
  }

  if (!bleConnected && !doConnect && millis() - lastScanTime > SCAN_INTERVAL) {
    Serial.println(F("[系统] 重新扫描..."));
    BLEDevice::getScan()->start(10, false);
    lastScanTime = millis();
  }

  delay(50);
}
