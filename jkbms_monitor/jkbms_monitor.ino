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
 *
 * 串口命令 (115200 波特率):
 *   scan      - 扫描并列出所有BLE设备
 *   connect   - 手动连接目标MAC
 *   mac XX:XX - 临时修改目标MAC后连接
 *   status    - 显示当前状态
 *   data      - 手动请求BMS数据
 *   restart   - 重启BLE重新扫描
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
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
String targetMAC = "98:DA:20:07:B9:00";
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
MyClientCallback* pClientCallback = nullptr;

bool bleConnected = false;
bool isConnecting = false;

uint8_t frameBuf[MAX_FRAME_SIZE];
int framePos = 0;
volatile bool newDataReady = false;

unsigned long lastDisplayUpdate = 0;
unsigned long lastCommandTime = 0;
unsigned long lastConnectAttempt = 0;
unsigned long connectFailCount = 0;
const unsigned long DISPLAY_INTERVAL = 500;
const unsigned long COMMAND_INTERVAL = 8000;
const unsigned long CONNECT_RETRY_FAST = 3000;
const unsigned long CONNECT_RETRY_SLOW = 30000;
const unsigned long DATA_TIMEOUT = 30000;

bool needFullRedraw = true;

// 扫描结果缓存
#define MAX_SCAN_RESULTS 20
struct ScanResult {
  String address;
  String name;
  int rssi;
};
ScanResult scanResults[MAX_SCAN_RESULTS];
int scanResultCount = 0;

String serialBuf = "";

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
uint32_t readU32LE(int o) {
  return (uint32_t)frameBuf[o] | ((uint32_t)frameBuf[o+1]<<8) |
         ((uint32_t)frameBuf[o+2]<<16) | ((uint32_t)frameBuf[o+3]<<24);
}
int32_t readI32LE(int o) { return (int32_t)readU32LE(o); }
uint16_t readU16BE(int o) { return ((uint16_t)frameBuf[o]<<8) | frameBuf[o+1]; }
int16_t readI16BE(int o) { return (int16_t)readU16BE(o); }

void parseFrame() {
  if (framePos < MIN_FRAME_SIZE) return;

  uint8_t crcCalc = calcCRC(frameBuf, framePos - 1);
  if (crcCalc != frameBuf[framePos - 1]) return;
  if (frameBuf[4] == 0x03) return;
  if (frameBuf[4] != FRAME_TYPE_CELL_INFO) return;

  float calcVoltage = 0;
  int cellCount = 0;
  for (int i = 0; i < 32; i++) {
    uint16_t cv = readU16BE(6 + i * 2);
    if (cv > 0) { calcVoltage += cv * 0.001f; cellCount++; }
  }

  bms.voltage = calcVoltage;
  bms.power = readU32LE(122) * 0.001f;
  bms.current = readI32LE(126) * 0.001f;
  bms.temp1 = readI16BE(130) * 0.1f;
  bms.temp2 = readI16BE(132) * 0.1f;
  bms.soc = frameBuf[141];
  bms.capacity_remain = readU32LE(142) * 0.001f;
  bms.capacity_nominal = readU32LE(146) * 0.001f;

  if (bms.soc == 0 || bms.soc > 100) {
    int soc2 = frameBuf[173];
    if (soc2 > 0 && soc2 <= 100) {
      bms.power = readU32LE(154) * 0.001f;
      bms.current = readI32LE(158) * 0.001f;
      bms.temp1 = readI16BE(162) * 0.1f;
      bms.temp2 = readI16BE(164) * 0.1f;
      bms.soc = soc2;
      bms.capacity_remain = readU32LE(174) * 0.001f;
      bms.capacity_nominal = readU32LE(178) * 0.001f;
    }
  }

  if (bms.voltage < 1.0f) bms.voltage = calcVoltage;
  bms.isCharging    = (bms.current > 0.05f);
  bms.isDischarging = (bms.current < -0.05f);
  bms.dataValid     = true;
  bms.lastUpdate    = millis();
  newDataReady      = true;

  Serial.printf("[BMS] V=%.2f P=%.1f I=%.2f SOC=%d%% T=%.1f\n",
    bms.voltage, bms.power, bms.current, bms.soc, bms.temp1);
}

// ===================== BLE 通知回调 =====================
void notifyCallback(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
  if (framePos > MAX_FRAME_SIZE) framePos = 0;
  if (length >= 4 && pData[0]==0x55 && pData[1]==0xAA && pData[2]==0xEB && pData[3]==0x90) {
    framePos = 0;
  }
  for (size_t i = 0; i < length; i++) {
    if (framePos < MAX_FRAME_SIZE) frameBuf[framePos++] = pData[i];
  }
  if (framePos >= MIN_FRAME_SIZE) {
    parseFrame();
    framePos = 0;
  }
}

// ===================== BLE 客户端回调 =====================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient*) {
    bleConnected = true;
    isConnecting = false;
    connectFailCount = 0;
    needFullRedraw = true;
    Serial.println(F("[BLE] 已连接"));
  }
  void onDisconnect(BLEClient*) {
    bleConnected = false;
    isConnecting = false;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
    needFullRedraw = true;
    lastConnectAttempt = 0;
    Serial.println(F("[BLE] 已断开,将自动重连"));
  }
};

// ===================== 扫描回调 =====================
class DebugScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    String addr = dev.getAddress().toString().c_str();
    String name = dev.haveName() ? dev.getName().c_str() : "";
    int rssi = dev.getRSSI();
    Serial.printf("[SCAN] %s  RSSI:%d  Name:%s\n", addr.c_str(), rssi, name.c_str());
    if (scanResultCount < MAX_SCAN_RESULTS) {
      scanResults[scanResultCount].address = addr;
      scanResults[scanResultCount].name = name;
      scanResults[scanResultCount].rssi = rssi;
      scanResultCount++;
    }
  }
};

// ===================== 停止扫描 =====================
void stopScan() {
  BLEScan* pScan = BLEDevice::getScan();
  if (pScan->isScanning()) pScan->stop();
}

// ===================== 清理客户端 =====================
void cleanupClient() {
  if (pClient) {
    if (pClient->isConnected()) pClient->disconnect();
    delete pClient;
    pClient = nullptr;
  }
  pWriteChar = nullptr;
  pNotifyChar = nullptr;
  bleConnected = false;
  isConnecting = false;
}

// ===================== 连接BMS核心逻辑 =====================
bool doConnectCore(BLEAddress& addr) {
  cleanupClient();

  pClient = BLEDevice::createClient();
  if (!pClientCallback) pClientCallback = new MyClientCallback();
  pClient->setClientCallbacks(pClientCallback);

  Serial.printf("[BLE] 连接 %s ...\n", addr.toString().c_str());

  if (!pClient->connect(addr, BLE_ADDR_TYPE_RANDOM)) {
    Serial.println(F("[BLE] 连接失败"));
    cleanupClient();
    return false;
  }

  BLERemoteService* pSvc = pClient->getService(serviceUUID);
  if (!pSvc) {
    Serial.println(F("[BLE] 未找到服务!"));
    cleanupClient();
    return false;
  }

  auto* charMap = pSvc->getCharacteristics();
  for (auto& kv : *charMap) {
    BLERemoteCharacteristic* c = kv.second;
    if (c->canWrite() && !pWriteChar) pWriteChar = c;
    if (c->canNotify() && !pNotifyChar) pNotifyChar = c;
  }

  if (!pNotifyChar) {
    Serial.println(F("[BLE] 未找到通知特征!"));
    cleanupClient();
    return false;
  }

  pNotifyChar->registerForNotify(notifyCallback);
  if (!pWriteChar && pNotifyChar && pNotifyChar->canWrite()) pWriteChar = pNotifyChar;

  delay(100);
  sendBMSCommand(CMD_CELL_INFO);
  lastCommandTime = millis();
  Serial.println(F("[BLE] 连接成功!"));
  return true;
}

// ===================== 自动连接(直连MAC,无需扫描) =====================
bool autoConnect() {
  if (isConnecting || bleConnected) return false;
  isConnecting = true;
  stopScan();

  BLEAddress addr(targetMAC.c_str());
  bool ok = doConnectCore(addr);

  if (!ok) {
    connectFailCount++;
    isConnecting = false;
    Serial.printf("[BLE] 失败%d次, %lus后重试\n",
      connectFailCount, (connectFailCount >= 3 ? CONNECT_RETRY_SLOW : CONNECT_RETRY_FAST) / 1000);
  }
  return ok;
}

// ===================== 手动连接 =====================
bool manualConnect(String mac) {
  stopScan();
  isConnecting = false;
  BLEAddress addr(mac.c_str());
  bool ok = doConnectCore(addr);
  if (!ok) Serial.println(F("[BLE] 手动连接失败!"));
  return ok;
}

// ===================== 串口命令处理 =====================
void handleSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "scan") {
    Serial.println(F("[SCAN] 扫描10秒..."));
    scanResultCount = 0;
    stopScan();
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(10, true);
    Serial.printf("[SCAN] 发现 %d 个设备:\n", scanResultCount);
    for (int i = 0; i < scanResultCount; i++) {
      Serial.printf("  [%d] %s  RSSI:%d  %s%s\n", i,
        scanResults[i].address.c_str(), scanResults[i].rssi,
        scanResults[i].name.c_str(),
        scanResults[i].address == targetMAC ? " <<<目标!" : "");
    }
  } else if (cmd == "connect") {
    Serial.printf("[CMD] 连接 %s\n", targetMAC.c_str());
    if (bleConnected) { cleanupClient(); delay(300); }
    manualConnect(targetMAC);
  } else if (cmd.startsWith("mac ")) {
    String m = cmd.substring(4); m.trim(); m.toUpperCase();
    if (m.length() == 17) {
      targetMAC = m;
      Serial.printf("[CMD] MAC已更新: %s\n", targetMAC.c_str());
    } else Serial.println(F("[CMD] 格式: XX:XX:XX:XX:XX:XX"));
  } else if (cmd == "status") {
    Serial.printf("[状态] BLE:%s MAC:%s 数据:%s\n",
      bleConnected?"已连接":"未连接", targetMAC.c_str(), bms.dataValid?"有效":"无");
    if (bms.dataValid)
      Serial.printf("  V=%.2f P=%.1f I=%.2f SOC=%d%% T=%.1f Cap=%.1f/%.1fAh\n",
        bms.voltage, bms.power, bms.current, bms.soc, bms.temp1,
        bms.capacity_remain, bms.capacity_nominal);
    Serial.printf("  内存:%d 失败:%d次\n", ESP.getFreeHeap(), connectFailCount);
  } else if (cmd == "data") {
    if (bleConnected) sendBMSCommand(CMD_CELL_INFO);
    else Serial.println(F("[CMD] 未连接!"));
  } else if (cmd == "restart") {
    cleanupClient();
    connectFailCount = 0;
    lastConnectAttempt = 0;
    needFullRedraw = true;
    delay(200);
    Serial.println(F("[CMD] 已重置,将自动重连"));
  } else if (cmd == "help") {
    Serial.println(F("命令: scan connect mac status data restart help"));
  } else {
    Serial.printf("[CMD] 未知: %s (help查看)\n", cmd.c_str());
  }
}

// ===================== 中文渲染核心 =====================
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
      bool isSet = (byteIdx >= 0 && byteIdx < 1024) ? ((buf[byteIdx] >> bit) & 1) : false;
      cnRgbBuf[py * textW + px] = isSet ? fgColor : C_BG;
    }
  }
  tft.drawRGBBitmap(x, y, cnRgbBuf, textW, textH);
}
void drawCN12(const char* t, int x, int y, uint16_t c) { drawCN(t, x, y, c, u8g2_font_wqy12_t_chinese3); }
void drawCN14(const char* t, int x, int y, uint16_t c) { drawCN(t, x, y, c, u8g2_font_wqy14_t_chinese3); }
void drawCN16(const char* t, int x, int y, uint16_t c) { drawCN(t, x, y, c, u8g2_font_wqy16_t_chinese3); }

// ===================== 显示辅助 =====================
uint16_t socColor(int s) { return s>60?C_GREEN:s>30?C_YELLOW:s>15?C_ORANGE:C_RED; }
uint16_t socGlow(int s) { return s>60?C_GLOW_G:s>30?C_GLOW_Y:s>15?C_ORANGE:C_GLOW_R; }
uint16_t pwrColor() { return bms.isCharging?C_GREEN:bms.isDischarging?C_RED:C_GRAY; }
uint16_t pwrGlow() { return bms.isCharging?C_GLOW_G:bms.isDischarging?C_GLOW_R:C_DGRAY; }

void drawCornerMarks(int x, int y, int w, int h, uint16_t c) {
  int cl = 6;
  tft.drawFastHLine(x, y, cl, c); tft.drawFastVLine(x, y, cl, c);
  tft.drawFastHLine(x+w-cl, y, cl, c); tft.drawFastVLine(x+w-1, y, cl, c);
  tft.drawFastHLine(x, y+h-1, cl, c); tft.drawFastVLine(x, y+h-cl, cl, c);
  tft.drawFastHLine(x+w-cl, y+h-1, cl, c); tft.drawFastVLine(x+w-1, y+h-cl, cl, c);
}

void drawGlowBar(int x, int y, int w, int h, int pct, uint16_t fillC, uint16_t glowC) {
  tft.fillRect(x, y, w, h, C_BAR_BG);
  int fw = (int)((w - 4) * (float)constrain(pct, 0, 100) / 100.0f);
  if (fw > 0) { tft.fillRect(x+2, y, fw, h, glowC); tft.fillRect(x+2, y+2, fw, h-4, fillC); }
  tft.drawFastHLine(x, y, w, C_DGRAY); tft.drawFastHLine(x, y+h-1, w, C_DGRAY);
}

void drawAccentLine(int y, uint16_t c) { tft.drawFastHLine(0, y, SW, c); tft.drawFastHLine(0, y+1, SW, C_BG); }

bool dataChanged() {
  return bms.power!=prev.power || bms.voltage!=prev.voltage || bms.current!=prev.current ||
         bms.temp1!=prev.temp1 || bms.soc!=prev.soc || bms.capacity_remain!=prev.cap_remain ||
         bms.isCharging!=prev.isCharging || bms.isDischarging!=prev.isDischarging ||
         bms.dataValid!=prev.dataValid || bleConnected!=prev.bleConnected;
}
void savePrev() {
  prev.power=bms.power; prev.voltage=bms.voltage; prev.current=bms.current; prev.temp1=bms.temp1;
  prev.soc=bms.soc; prev.cap_remain=bms.capacity_remain;
  prev.isCharging=bms.isCharging; prev.isDischarging=bms.isDischarging;
  prev.dataValid=bms.dataValid; prev.bleConnected=bleConnected;
}

// ===================== 静态元素 =====================
void drawStatic() {
  drawAccentLine(HDR_H, C_ACCENT);
  drawCornerMarks(2, PWR_Y, SW-4, PWR_H, C_DGRAY);
  tft.drawFastVLine(6, PWR_Y+2, 2, pwrGlow());
  drawCornerMarks(2, BAT_Y, SW-4, BAT_H, C_DGRAY);
  tft.drawFastVLine(6, BAT_Y+2, 2, socGlow(bms.soc));
  drawCornerMarks(2, BOT_Y, SW-4, BOT_H, C_DGRAY);
  drawCN12("功率", 10, PWR_Y+6, C_GRAY);
  drawCN12("电池", 10, BAT_Y+6, C_GRAY);
  tft.drawFastVLine(78, BOT_Y+6, BOT_H-12, C_DGRAY);
  tft.drawFastVLine(158, BOT_Y+6, BOT_H-12, C_DGRAY);
  drawCN12("温度", 14, BOT_Y+6, C_CYAN);
  drawCN12("电压", 90, BOT_Y+6, C_YELLOW);
  drawCN12("电流", 168, BOT_Y+6, C_ORANGE);
}

// ===================== 动态元素 =====================
void drawDynamic() {
  tft.fillRect(8, 4, 170, 14, C_BG);
  drawCN14("JK BMS 监控", 8, 4, C_DCYAN);
  tft.fillRect(8, 20, 170, 10, C_BG);
  tft.setTextSize(1); tft.setTextColor(C_DGRAY); tft.setCursor(8, 20); tft.print(BMS_NAME);

  float pulse = (sin(millis()/600.0*PI)+1.0)*0.5;
  if (bleConnected) {
    tft.fillCircle(SW-12, 14, 4, C_GREEN); tft.drawCircle(SW-12, 14, 4+(int)(pulse*2), C_DGREEN);
  } else {
    tft.fillCircle(SW-12, 14, 4, C_RED); tft.drawCircle(SW-12, 14, 4+(int)(pulse*2), C_DRED);
  }

  uint16_t pc=pwrColor(), pg=pwrGlow();
  tft.fillRect(0, PWR_Y, SW, 2, pg);

  float absP = fabs(bms.power);
  char ps[16];
  if (!bms.dataValid) strcpy(ps, "---");
  else if (absP>=1000) dtostrf(absP,1,0,ps);
  else if (absP>=100) dtostrf(absP,1,1,ps);
  else dtostrf(absP,1,2,ps);

  tft.fillRect(10, PWR_Y+20, 220, 44, C_BG);
  tft.setTextSize(4); tft.setTextColor(pc); tft.setCursor(10, PWR_Y+22);
  if (bms.isCharging && bms.dataValid) tft.print(F("+"));
  else if (bms.isDischarging && bms.dataValid) tft.print(F("-"));
  tft.print(ps);
  tft.setTextSize(2); tft.setTextColor(pc);
  int uw = 10+(bms.isCharging||bms.isDischarging?24:0)+strlen(ps)*24;
  if (uw>SW-30) uw=SW-30;
  tft.setCursor(uw, PWR_Y+38); tft.print(F("W"));

  tft.fillRect(10, PWR_Y+66, 180, 18, C_BG);
  if (!bms.dataValid) drawCN14("等待数据...", 10, PWR_Y+66, C_GRAY);
  else if (bms.isCharging) drawCN14("\xe2\x96\xb2 充电中", 10, PWR_Y+66, C_GREEN);
  else if (bms.isDischarging) drawCN14("\xe2\x96\xbc 放电中", 10, PWR_Y+66, C_RED);
  else drawCN14("-- 待机", 10, PWR_Y+66, C_GRAY);

  int pwrPct = bms.dataValid ? constrain((int)(absP/30.0*100),0,100) : 0;
  drawGlowBar(10, PWR_Y+86, SW-20, 8, pwrPct, pc, pg);

  uint16_t sc=socColor(bms.soc), sg=socGlow(bms.soc);
  char ss[8];
  if (bms.dataValid) sprintf(ss, "%d%%", bms.soc); else strcpy(ss, "--%");
  tft.fillRect(10, BAT_Y+20, 200, 44, C_BG);
  tft.setTextSize(4); tft.setTextColor(C_WHITE); tft.setCursor(10, BAT_Y+22); tft.print(ss);
  tft.setTextSize(2); tft.setTextColor(sc); tft.setCursor(10+strlen(ss)*24, BAT_Y+38); tft.print(F(" "));
  drawGlowBar(10, BAT_Y+68, SW-20, 16, bms.dataValid?bms.soc:0, sc, sg);

  tft.fillRect(10, BAT_Y+88, 220, 14, C_BG);
  tft.setTextSize(1); tft.setTextColor(C_GRAY); tft.setCursor(10, BAT_Y+90);
  if (bms.dataValid) { char cs[32]; sprintf(cs, "%.1f / %.1f Ah", bms.capacity_remain, bms.capacity_nominal); tft.print(cs); }
  else tft.print(F("--- / --- Ah"));
  float used = bms.capacity_nominal - bms.capacity_remain;
  if (bms.dataValid && used > 0) {
    char us[16]; sprintf(us, "-%.1fAh", used);
    int16_t x1,y1; uint16_t w,h; tft.getTextBounds(us,0,0,&x1,&y1,&w,&h);
    tft.setCursor(SW-10-w, BAT_Y+90); tft.setTextColor(C_DRED); tft.print(us);
  }

  tft.fillRect(8, BOT_Y+22, 66, 38, C_BG);
  tft.setTextSize(2); tft.setTextColor(C_WHITE); tft.setCursor(10, BOT_Y+26);
  if (bms.dataValid) { char ts[10]; dtostrf(bms.temp1,1,1,ts); tft.print(ts); tft.setTextSize(1); tft.print(F("C")); }
  else tft.print(F("--"));

  tft.fillRect(84, BOT_Y+22, 66, 38, C_BG);
  tft.setTextSize(2); tft.setTextColor(C_WHITE); tft.setCursor(86, BOT_Y+26);
  if (bms.dataValid) { char vs[10]; dtostrf(bms.voltage,1,1,vs); tft.print(vs); tft.setTextSize(1); tft.print(F("V")); }
  else tft.print(F("--"));

  tft.fillRect(162, BOT_Y+22, 72, 38, C_BG);
  tft.setTextSize(2); tft.setTextColor(C_WHITE); tft.setCursor(164, BOT_Y+26);
  if (bms.dataValid) { char is2[10]; dtostrf(bms.current,1,2,is2); tft.print(is2); tft.setTextSize(1); tft.print(F("A")); }
  else tft.print(F("--"));

  tft.fillRect(162, BOT_Y+50, 72, 12, C_BG);
  if (bms.dataValid && (millis()-bms.lastUpdate > DATA_TIMEOUT)) drawCN12("超时!", 180, BOT_Y+50, C_RED);
  else if (bleConnected) drawCN12("在线", 186, BOT_Y+50, C_DGREEN);
}

// ===================== 主绘制 =====================
void drawDashboard() {
  if (needFullRedraw) {
    tft.fillScreen(C_BG); drawStatic(); drawDynamic();
    needFullRedraw = false; savePrev(); return;
  }
  if (!dataChanged() && !newDataReady) return;
  newDataReady = false; drawDynamic(); savePrev();
}

// ===================== 启动画面 =====================
void drawSplash() {
  tft.fillScreen(C_BG);
  for (int i=0;i<3;i++) tft.drawFastHLine(30+i*8, 80+i*4, SW-60-i*16, C_ACCENT);
  for (int i=0;i<3;i++) tft.drawFastHLine(30+i*8, 220+i*4, SW-60-i*16, C_ACCENT);
  drawCN16("JK BMS", 60, 96, C_CYAN);
  drawCN14("蓝牙监控仪表盘", 35, 126, C_GRAY);
  tft.setTextSize(1); tft.setTextColor(C_DCYAN); tft.setCursor(50, 156); tft.print(BMS_NAME);
  drawCN12("连接中...", 76, 186, C_GRAY);
  drawCornerMarks(20, 74, SW-40, 160, C_DGRAY);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println(F("================================"));
  Serial.println(F("  JK BMS 蓝牙监控仪表盘"));
  Serial.println(F("================================"));

  bms = {0}; bms.dataValid = false;
  memset(&prev, 0xFF, sizeof(prev)); prev.bleConnected = true;
  needFullRedraw = true;

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(SW, SH); tft.setRotation(0); tft.invertDisplay(true); tft.fillScreen(C_BG);
  u8g2CN.setFont(u8g2_font_wqy12_t_chinese3); u8g2CN.setFontMode(1); u8g2CN.setFontPosBaseline();
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

  drawSplash();

  Serial.printf("[系统] 目标MAC: %s\n", targetMAC.c_str());
  BLEDevice::init("");
  Serial.println(F("[系统] 直接连接BMS..."));
}

// ===================== LOOP =====================
void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (serialBuf.length()>0) { handleSerialCommand(serialBuf); serialBuf=""; } }
    else { serialBuf += c; if (serialBuf.length()>60) serialBuf=""; }
  }

  // 自动重连逻辑
  if (!bleConnected && !isConnecting) {
    unsigned long now = millis();
    unsigned long retryInterval = (connectFailCount >= 3) ? CONNECT_RETRY_SLOW : CONNECT_RETRY_FAST;
    if (lastConnectAttempt == 0 || (now - lastConnectAttempt) >= retryInterval) {
      lastConnectAttempt = now;
      autoConnect();
    }
  }

  // 定时请求BMS数据
  if (bleConnected && millis() - lastCommandTime > COMMAND_INTERVAL) {
    sendBMSCommand(CMD_CELL_INFO);
    lastCommandTime = millis();
  }

  // 刷新显示
  if (millis() - lastDisplayUpdate > DISPLAY_INTERVAL) {
    drawDashboard();
    lastDisplayUpdate = millis();
  }

  delay(20);
}
