/*
 * JK-BMS 战斗仪表 - ESP32 + ST7789P3 2.8寸横屏显示
 * ================================================
 *
 * 使用前必须配置 TFT_eSPI 库的 User_Setup.h 文件:
 *
 * 在 ArduinoDroid 中, 找到 TFT_eSPI 库目录下的 User_Setup.h,
 * 修改以下内容 (注释掉其他驱动, 取消注释 ST7789):
 *
 *   #define ST7789_DRIVER
 *   #define TFT_WIDTH  240
 *   #define TFT_HEIGHT 320
 *
 *   #define TFT_MOSI 23
 *   #define TFT_SCLK 18
 *   #define TFT_CS   15
 *   #define TFT_DC    2
 *   #define TFT_RST   4
 *   #define TFT_BL   32
 *
 *   #define SPI_FREQUENCY  40000000
 *   #define SPI_READ_FREQUENCY  20000000
 *
 * 接线对照:
 *   ST7789  ->  ESP32
 *   VCC     ->  3.3V
 *   GND     ->  GND
 *   SCL/SCK ->  GPIO 18
 *   SDA/MOSI->  GPIO 23
 *   RES/RST ->  GPIO 4
 *   DC      ->  GPIO 2
 *   CS      ->  GPIO 15
 *   BLK     ->  GPIO 32 (或 3.3V 常亮)
 *
 * JK-BMS 信息:
 *   名称: JK_BD4A24S10P
 *   MAC:  98:da:20:07:b9:00
 *   协议: JK02_32S
 *
 * 功能特性:
 *   - BLE 蓝牙自动连接 JK-BMS
 *   - 战斗风格 UI, 功率越大颜色越激烈
 *   - 旋转光环动态效果 + 粒子飞散效果
 *   - 全中文界面, 圆角卡片设计
 *   - 右上角蓝牙连接状态指示(呼吸灯效果)
 *   - 主视觉: 实时功率(大字突出)
 *   - 次展示: 已用/总电池容量 + 进度条
 *   - 底部: 电池温度, 电流, 状态
 */

#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

TFT_eSPI tft = TFT_eSPI();

const char *BMS_NAME = "JK_BD4A24S10P";
const char *BMS_MAC  = "98:da:20:07:b9:00";

const BLEUUID JK_SERVICE_UUID("0000ffe0-0000-1000-8000-00805f9b34fb");
const BLEUUID JK_CHAR_UUID("0000ffe1-0000-1000-8000-00805f9b34fb");

const uint8_t PREAMBLE[4] = {0x55, 0xAA, 0xEB, 0x90};
const uint8_t CMD_CELL_INFO = 0x96;
const uint8_t CMD_DEVICE_INFO = 0x97;
const uint16_t MIN_FRAME = 300;
const uint16_t MAX_FRAME = 400;

BLEClient              *pClient = nullptr;
BLERemoteCharacteristic *pRemoteChar = nullptr;
bool bleConnected = false;
bool deviceFound  = false;
BLEAdvertisedDevice foundDevice;

uint8_t frameBuf[400];
uint16_t framePos = 0;

struct BmsData {
  float totalVoltage = 0;
  float current = 0;
  float power = 0;
  float capacityRemain = 0;
  float capacityTotal = 100;
  uint8_t soc = 0;
  float tempBatt = 0;
  float tempMos = 0;
  float avgCellV = 0;
  float minCellV = 0;
  float maxCellV = 0;
  bool charging = false;
  bool discharging = false;
  bool balancing = false;
};

BmsData bms;

float smoothPower = 0;
float smoothCurrent = 0;
unsigned long lastDataMs = 0;
unsigned long lastDrawMs = 0;
unsigned long lastCmdMs = 0;
unsigned long animFrame = 0;

uint8_t calcCRC(uint8_t *d, uint16_t n) {
  uint8_t c = 0;
  for (uint16_t i = 0; i < n; i++) c += d[i];
  return c;
}

int16_t getI16(uint8_t *b, int o) {
  return (int16_t)(b[o] | (b[o + 1] << 8));
}

uint16_t getU16(uint8_t *b, int o) {
  return (uint16_t)(b[o] | (b[o + 1] << 8));
}

uint32_t getU32(uint8_t *b, int o) {
  return (uint32_t)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
}

int32_t getI32(uint8_t *b, int o) {
  return (int32_t)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
}

class ScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (dev.haveName() && dev.getName() == BMS_NAME) {
      dev.getScan()->stop();
      foundDevice = dev;
      deviceFound = true;
      Serial.println("扫描到 JK-BMS 设备");
    }
  }
};

class ClientCB : public BLEClientCallbacks {
  void onConnect(BLEClient *c) {}
  void onDisconnect(BLEClient *c) {
    bleConnected = false;
    Serial.println("BLE 已断开");
  }
};

void writeCmd(uint8_t cmd, uint32_t val, uint8_t len) {
  if (!bleConnected || !pRemoteChar) return;
  uint8_t f[20] = {0};
  f[0] = 0xAA;
  f[1] = 0x55;
  f[2] = 0x90;
  f[3] = 0xEB;
  f[4] = cmd;
  f[5] = len;
  f[6] = (uint8_t)val;
  f[7] = (uint8_t)(val >> 8);
  f[8] = (uint8_t)(val >> 16);
  f[9] = (uint8_t)(val >> 24);
  pRemoteChar->writeValue(f, 20, false);
}

void notifyCB(BLERemoteCharacteristic *c, uint8_t *d, size_t n, bool isNotify) {
  for (size_t i = 0; i < n; i++) {
    if (framePos < 4) {
      if (d[i] == PREAMBLE[framePos]) frameBuf[framePos++] = d[i];
      else { framePos = 0; if (d[i] == PREAMBLE[0]) frameBuf[framePos++] = d[i]; }
    } else {
      frameBuf[framePos++] = d[i];
      if (framePos >= MIN_FRAME && framePos <= MAX_FRAME) {
        uint8_t ck = calcCRC(frameBuf, framePos - 1);
        if (ck == frameBuf[framePos - 1]) {
          parseFrame(frameBuf, framePos);
          framePos = 0;
        }
      }
      if (framePos >= MAX_FRAME) framePos = 0;
    }
  }
}

void parseFrame(uint8_t *b, uint16_t len) {
  if (len < 11) return;
  uint8_t type = b[4];
  uint8_t ver  = b[5];

  uint8_t ck = calcCRC(b, len - 1);
  if (ck != b[len - 1]) return;

  if (type == 0x02) {
    parseCellInfo(b, len, ver);
    lastDataMs = millis();
  }
}

void parseCellInfo(uint8_t *b, uint16_t len, uint8_t ver) {
  if (ver == 0x03) {
    parseJK02_32S(b, len);
  } else if (ver == 0x02) {
    parseJK02_24S(b, len);
  }
}

void parseJK02_32S(uint8_t *b, uint16_t len) {
  int off = 6;

  float cellV[32];
  int cc = 0;
  float sum = 0, mn = 99, mx = 0;

  for (int i = 0; i < 32; i++) {
    float v = getU16(b, off) * 0.001f;
    off += 2;
    if (v > 0.01f) { cellV[i] = v; sum += v; if (v < mn) mn = v; if (v > mx) mx = v; cc++; }
    else cellV[i] = 0;
  }

  for (int i = 0; i < 32; i++) off += 2;

  if (cc > 0) {
    bms.avgCellV = sum / cc;
    bms.minCellV = mn;
    bms.maxCellV = mx;
  }

  bms.totalVoltage = getU32(b, off) * 0.001f;
  off += 4;
  bms.current = getI32(b, off) * 0.001f;
  off += 4;
  bms.power = bms.totalVoltage * bms.current;
  off += 4;
  bms.tempBatt = getU16(b, off) * 0.1f;
  off += 2;
  bms.tempMos  = getU16(b, off) * 0.1f;
  off += 2;

  off += 6;
  bms.soc = b[off];
  off++;

  bms.capacityRemain = getU32(b, off) * 0.001f;
  off += 4;
  bms.capacityTotal  = getU32(b, off) * 0.001f;
  off += 4;

  uint8_t flag = b[off];
  bms.charging    = (flag & 0x01) != 0;
  bms.discharging = (flag & 0x02) != 0;
  bms.balancing   = (flag & 0x04) != 0;
}

void parseJK02_24S(uint8_t *b, uint16_t len) {
  int off = 6;

  float cellV[24];
  int cc = 0;
  float sum = 0, mn = 99, mx = 0;

  for (int i = 0; i < 24; i++) {
    float v = getU16(b, off) * 0.001f;
    off += 2;
    if (v > 0.01f) { cellV[i] = v; sum += v; if (v < mn) mn = v; if (v > mx) mx = v; cc++; }
    else cellV[i] = 0;
  }

  for (int i = 0; i < 24; i++) off += 2;

  if (cc > 0) {
    bms.avgCellV = sum / cc;
    bms.minCellV = mn;
    bms.maxCellV = mx;
  }

  bms.totalVoltage = getU32(b, off) * 0.001f;
  off += 4;
  bms.current = getI32(b, off) * 0.001f;
  off += 4;
  bms.power = bms.totalVoltage * bms.current;
  off += 4;
  bms.tempBatt = getU16(b, off) * 0.1f;
  off += 2;
  bms.tempMos  = getU16(b, off) * 0.1f;
  off += 2;

  off += 6;
  bms.soc = b[off];
  off++;

  bms.capacityRemain = getU32(b, off) * 0.001f;
  off += 4;
  bms.capacityTotal  = getU32(b, off) * 0.001f;
  off += 4;

  uint8_t flag = b[off];
  bms.charging    = (flag & 0x01) != 0;
  bms.discharging = (flag & 0x02) != 0;
  bms.balancing   = (flag & 0x04) != 0;
}

bool connectBMS() {
  if (!deviceFound) return false;

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCB());

  if (!pClient->connect(&foundDevice)) {
    Serial.println("BLE 连接失败");
    return false;
  }

  BLERemoteService *svc = pClient->getService(JK_SERVICE_UUID);
  if (!svc) {
    Serial.println("未找到 JK-BMS 服务");
    pClient->disconnect();
    return false;
  }

  pRemoteChar = svc->getCharacteristic(JK_CHAR_UUID);
  if (!pRemoteChar) {
    Serial.println("未找到 JK-BMS 特征");
    pClient->disconnect();
    return false;
  }

  if (pRemoteChar->canNotify())
    pRemoteChar->registerForNotify(notifyCB);

  bleConnected = true;
  Serial.println("JK-BMS 已连接");

  writeCmd(CMD_DEVICE_INFO, 0, 0);
  delay(300);
  writeCmd(CMD_CELL_INFO, 0, 0);

  return true;
}

void scanBMS() {
  BLEScan *s = BLEDevice::getScan();
  s->setAdvertisedDeviceCallbacks(new ScanCB());
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(99);
  s->start(10, false);
}

bool initBLE() {
  BLEDevice::init("");
  scanBMS();

  unsigned long st = millis();
  while (!deviceFound && (millis() - st < 15000)) delay(50);

  if (deviceFound) {
    if (connectBMS()) {
      delay(500);
      writeCmd(CMD_CELL_INFO, 0, 0);
      return true;
    }
  }
  return false;
}

uint16_t battleColor(float p) {
  float a = abs(p);
  if (a < 50) return tft.color565(60, 190, 110);
  if (a < 300) { uint8_t r = (uint8_t)(60 + (a - 50) * (195.0 / 250)); uint8_t g = (uint8_t)(190 - (a - 50) * (30.0 / 250)); return tft.color565(r, g, 100); }
  if (a < 1000) { uint8_t g = (uint8_t)(160 - (a - 300) * (90.0 / 700)); return tft.color565(255, g, 50); }
  if (a < 2500) { uint8_t g = (uint8_t)(70 - (a - 1000) * (55.0 / 1500)); return tft.color565(255, g > 255 ? 255 : g, 10); }
  return tft.color565(255, 15, 0);
}

uint16_t battleBg(float p) {
  float a = abs(p);
  if (a < 50) return tft.color565(14, 20, 18);
  if (a < 300) { uint8_t r = (uint8_t)(14 + (a - 50) * (8.0 / 250)); uint8_t g = (uint8_t)(20 - (a - 50) * (8.0 / 250)); return tft.color565(r, g, 12); }
  if (a < 1000) { uint8_t r = (uint8_t)(22 + (a - 300) * (13.0 / 700)); return tft.color565(r, 10, 5); }
  return tft.color565(35, 5, 2);
}

uint16_t cardBg() { return tft.color565(22, 28, 32); }

uint16_t cardBorder(float p) {
  float a = abs(p);
  if (a < 50) return tft.color565(45, 55, 50);
  if (a < 300) { uint8_t g = (uint8_t)(55 + (a - 50) * (130.0 / 250)); return tft.color565(50, g, 40); }
  if (a < 1000) { uint8_t r = (uint8_t)(60 + (a - 300) * (195.0 / 700)); return tft.color565(r, 200, 20); }
  if (a < 2500) return tft.color565(255, (uint8_t)(200 - (a - 1000) * 160.0 / 1500), 10);
  return tft.color565(255, 20, 5);
}

void drawCard(int x, int y, int w, int h, int r, float p) {
  tft.fillRoundRect(x, y, w, h, r, cardBorder(p));
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, r - 1, cardBg());
}

void drawCardNoBorder(int x, int y, int w, int h, int r) {
  tft.fillRoundRect(x, y, w, h, r, tft.color565(28, 34, 38));
  tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, cardBg());
}

void drawBTSymbol(int x, int y) {
  uint16_t cl;
  const char *lb;
  if (bleConnected) {
    if (millis() - lastDataMs < 5000) { cl = tft.color565(0, 230, 110); lb = "已连接"; }
    else { cl = tft.color565(255, 210, 60); lb = "等待数据"; }
  } else { cl = tft.color565(255, 55, 45); lb = "未连接"; }

  tft.fillRoundRect(x - 5, y - 4, 10, 10, 5, cl);

  if (bleConnected && millis() - lastDataMs < 5000) {
    int phase = (millis() / 600) % 2;
    if (phase) {
      tft.fillRoundRect(x - 8, y - 7, 16, 16, 8, tft.color565(0, 80, 40));
      tft.fillRoundRect(x - 5, y - 4, 10, 10, 5, cl);
    }
  }

  tft.setTextColor(cl, battleBg(smoothPower));
  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(2);
  tft.drawString(lb, x - 5, y + 8);
}

void drawHeader() {
  uint16_t bg = battleBg(smoothPower);
  tft.setTextColor(tft.color565(170, 175, 165), bg);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("JK-BMS 战斗仪表", 4, 2);
  drawBTSymbol(318, 6);
}

void drawParticles(float p) {
  float a = abs(p);
  int n = (int)(a / 400.0f);
  if (n > 10) n = 10;
  for (int i = 0; i < n; i++) {
    int px = random(0, 320);
    int py = random(0, 240);
    int sz = random(1, 3);
    uint16_t cl = battleColor(p);
    if (sz == 1) tft.drawPixel(px, py, cl);
    else tft.fillRect(px, py, sz, sz, cl);
  }
}

void drawPowerRing(int cx, int cy, int r, float p) {
  float a = abs(p);
  float ratio = a / 5000.0f;
  if (ratio > 1.0f) ratio = 1.0f;

  for (int i = 1; i <= 5; i++) {
    int ri = r - i * 4;
    if (ri < 10) ri = 10;
    uint16_t rc;
    if (i <= (int)(ratio * 5 + 1)) {
      rc = tft.color565(
        (uint8_t)(180 - i * 25),
        (uint8_t)(160 - i * 35),
        (uint8_t)(80 - i * 18)
      );
    } else {
      rc = tft.color565(16, 22, 20);
    }
    tft.drawCircle(cx, cy, ri, rc);
  }

  int segments = 12;
  for (int i = 0; i < segments; i++) {
    float angle = TWO_PI * i / segments + millis() * 0.001f * (1.0f + ratio * 3.0f);
    int x1 = cx + (r - 30) * cos(angle);
    int y1 = cy + (r - 30) * sin(angle);
    int x2 = cx + (r - 15) * cos(angle);
    int y2 = cy + (r - 15) * sin(angle);

    if (i < (int)(ratio * segments)) {
      tft.drawLine(x1, y1, x2, y2, battleColor(p));
    } else {
      tft.drawLine(x1, y1, x2, y2, tft.color565(20, 26, 22));
    }
  }
}

void drawMainPower() {
  int x = 4, y = 22, w = 210, h = 120;
  drawCard(x, y, w, h, 10, smoothPower);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("实时功率", x + 10, y + 8);

  float absP = abs(smoothPower);
  char ps[24];
  if (absP >= 1000) snprintf(ps, sizeof(ps), "%.2f", absP / 1000.0f);
  else snprintf(ps, sizeof(ps), "%.0f", absP);

  uint16_t mc = battleColor(smoothPower);
  tft.setTextColor(mc, cardBg());
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(7);
  tft.drawString(ps, x + w / 2, y + h / 2 - 10);

  tft.setTextFont(4);
  const char *unit = absP >= 1000 ? "kW" : "W";
  tft.drawString(unit, x + w / 2, y + h / 2 + 22);

  tft.setTextFont(2);
  if (smoothPower > 10) {
    tft.setTextColor(tft.color565(0, 210, 130), cardBg());
    tft.drawString("高能释放", x + w / 2, y + h - 14);
  } else if (smoothPower < -10) {
    tft.setTextColor(tft.color565(255, 180, 50), cardBg());
    tft.drawString("能量注入", x + w / 2, y + h - 14);
  } else {
    tft.setTextColor(tft.color565(90, 95, 90), cardBg());
    tft.drawString("待命状态", x + w / 2, y + h - 14);
  }

  drawPowerRing(x + w / 2, y + h / 2 - 4, (w - 20) / 2, smoothPower);
}

void drawTempPanel() {
  int x = 220, y = 22, w = 96, h = 66;
  drawCardNoBorder(x, y, w, h, 8);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("电池温度", x + 8, y + 5);

  char ts[16];
  snprintf(ts, sizeof(ts), "%.1f", bms.tempBatt);
  uint16_t tc;
  if (bms.tempBatt < 25) tc = tft.color565(60, 200, 255);
  else if (bms.tempBatt < 40) tc = tft.color565(100, 230, 100);
  else if (bms.tempBatt < 55) tc = tft.color565(255, 190, 40);
  else tc = tft.color565(255, 50, 20);

  tft.setTextColor(tc, cardBg());
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(6);
  tft.drawString(ts, x + w / 2 - 8, y + h / 2 + 4);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, cardBg());
  tft.drawString("\260C", x + w / 2 + 28, y + h / 2 - 10);
}

void drawVoltPanel() {
  int x = 220, y = 94, w = 96, h = 48;
  drawCardNoBorder(x, y, w, h, 8);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("总电压", x + 8, y + 4);

  char vs[12];
  snprintf(vs, sizeof(vs), "%.1f", bms.totalVoltage);
  tft.setTextColor(tft.color565(80, 190, 255), cardBg());
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(vs, x + w / 2 - 10, y + h / 2 + 2);

  tft.setTextFont(2);
  tft.drawString("V", x + w / 2 + 24, y + h / 2 - 10);

  tft.setTextColor(tft.color565(100, 105, 100), cardBg());
  char cs[24];
  snprintf(cs, sizeof(cs), "单体 %.2f", bms.avgCellV);
  tft.drawString(cs, x + w / 2 - 10, y + h - 6);
}

void drawCapPanel() {
  int x = 4, y = 148, w = 312, h = 80;
  drawCardNoBorder(x, y, w, h, 8);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("电池容量", x + 8, y + 5);

  char cs[32];
  snprintf(cs, sizeof(cs), "%.1f / %.1f Ah", bms.capacityRemain, bms.capacityTotal);

  tft.setTextColor(tft.color565(210, 215, 200), cardBg());
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(cs, x + w / 2, y + h / 2 - 12);

  float ratio = (bms.capacityTotal > 0) ? bms.capacityRemain / bms.capacityTotal : 0;
  if (ratio < 0) ratio = 0;
  if (ratio > 1) ratio = 1;

  int bx = x + 8, by = y + h - 20, bw = w - 85, bh = 12;
  tft.fillRoundRect(bx, by, bw, bh, 6, tft.color565(8, 12, 14));

  int fw = (int)(bw * ratio);
  if (fw > 0) {
    uint16_t bc;
    if (ratio > 0.7f) bc = tft.color565(0, 230, 110);
    else if (ratio > 0.3f) bc = tft.color565(255, 185, 35);
    else bc = tft.color565(255, 45, 25);

    if (fw > 6) {
      tft.fillRoundRect(bx, by, fw, bh, 6, bc);
      tft.fillRect(bx + fw - 4, by, 4, bh, bc);
    } else {
      tft.fillRect(bx, by, fw, bh, bc);
    }
  }

  char socs[8];
  snprintf(socs, sizeof(socs), "%d%%", bms.soc);
  tft.setTextColor(tft.color565(200, 205, 195), cardBg());
  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(4);
  tft.drawString(socs, x + w - 12, y + 5);

  int sx = x + w - 68;
  tft.drawRect(sx, y + 8, 22, 12, tft.color565(100, 110, 105));
  tft.fillRect(sx + 22, y + 10, 3, 8, tft.color565(100, 110, 105));

  int lvl = bms.soc;
  int fl = lvl > 0 ? (int)(20.0f * lvl / 100.0f) : 0;
  if (fl > 0) {
    uint16_t fcl = lvl > 60 ? tft.color565(0, 230, 110) : (lvl > 20 ? tft.color565(255, 185, 35) : tft.color565(255, 45, 25));
    tft.fillRect(sx + 1, y + 9, fl, 10, fcl);
  }
}

void drawCurrentBadge() {
  int x = 4, y = 233, w = 152, h = 37;
  drawCardNoBorder(x, y, w, h, 6);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("电流", x + 6, y + 4);

  float a = abs(smoothCurrent);
  char is[14];
  snprintf(is, sizeof(is), "%.1f A", a);

  uint16_t ic;
  if (smoothCurrent > 1) ic = tft.color565(0, 220, 130);
  else if (smoothCurrent < -1) ic = tft.color565(255, 180, 50);
  else ic = tft.color565(140, 145, 140);

  tft.setTextColor(ic, cardBg());
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(4);
  tft.drawString(is, x + w - 6, y + h / 2 + 2);
}

void drawStatusBadge() {
  int x = 162, y = 233, w = 154, h = 37;
  drawCardNoBorder(x, y, w, h, 6);

  tft.setTextColor(tft.color565(130, 140, 135), cardBg());
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("状态", x + 6, y + 4);

  const char *st;
  uint16_t sc;
  if (smoothPower > 10) { st = "放电"; sc = tft.color565(0, 220, 130); }
  else if (smoothPower < -10) { st = "充电"; sc = tft.color565(255, 180, 50); }
  else { st = "待机"; sc = tft.color565(120, 125, 120); }

  tft.setTextColor(sc, cardBg());
  tft.setTextDatum(MR_DATUM);
  tft.setTextFont(4);
  tft.drawString(st, x + w - 6, y + h / 2 + 2);

  if (bms.balancing) {
    tft.setTextColor(tft.color565(120, 200, 255), cardBg());
    tft.setTextFont(2);
    tft.drawString("均衡中", x + 6, y + h - 10);
  }
}

void renderUI() {
  uint16_t bg = battleBg(smoothPower);
  tft.fillScreen(bg);
  drawHeader();
  drawMainPower();
  drawTempPanel();
  drawVoltPanel();
  drawCapPanel();
  drawCurrentBadge();
  drawStatusBadge();
  drawParticles(smoothPower);
  animFrame++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n************************************");
  Serial.println("  JK-BMS 战斗仪表 v1.0");
  Serial.println("  ESP32-32E + ST7789P3 2.8\"");
  Serial.println("  协议: JK02_32S");
  Serial.println("************************************\n");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);

  tft.drawString("JK-BMS", 160, 80);
  tft.drawString("战斗仪表", 160, 105);
  tft.drawString("正在扫描蓝牙...", 160, 140);

  bms.capacityTotal = 100;

  if (initBLE()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(tft.color565(0, 230, 110), TFT_BLACK);
    tft.drawString("连接成功!", 160, 100);
    tft.drawString("等待数据...", 160, 130);
    delay(1500);
    tft.fillScreen(battleBg(0));
    renderUI();
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(tft.color565(255, 55, 45), TFT_BLACK);
    tft.drawString("连接失败!", 160, 100);
    tft.drawString("将自动重试...", 160, 130);
    Serial.println("初始连接失败, 进入重连循环");
  }
}

void loop() {
  unsigned long now = millis();

  if (!bleConnected) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.drawString("蓝牙已断开", 160, 100);
    tft.drawString("正在重连...", 160, 130);
    delay(1000);

    if (!deviceFound) {
      BLEDevice::deinit(true);
      delay(500);
      BLEDevice::init("");
      scanBMS();
      unsigned long ws = millis();
      while (!deviceFound && (millis() - ws < 12000)) delay(50);
    }

    if (deviceFound) connectBMS();
    delay(1000);
    return;
  }

  if (bleConnected && (now - lastDataMs < 5000)) {
    float sf = 0.25f;
    smoothPower = smoothPower * (1 - sf) + bms.power * sf;
    smoothCurrent = smoothCurrent * (1 - sf) + bms.current * sf;
  }

  if (now - lastDrawMs > 180) {
    lastDrawMs = now;
    renderUI();
  }

  if (bleConnected && (now - lastCmdMs) > 2500) {
    lastCmdMs = now;
    writeCmd(CMD_CELL_INFO, 0, 0);
  }

  delay(5);
}