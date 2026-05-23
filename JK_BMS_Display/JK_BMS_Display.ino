/***************************************************
 * JK-BMS BLE Monitor for ESP32-32E + ST7789 2.8" TFT
 * Protocol: JK02_32S
 * BMS MAC: 98:da:20:07:b9:00
 * BMS Name: JK_BD4A24S10P
 * Display: 320x240 landscape (ST7789)
 * Style: Combat/Sport Mode - 战斗风格运动模式
 ***************************************************/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ============== CONFIGURATION ==============
#define BMS_MAC_ADDRESS     "98:da:20:07:b9:00"
#define BMS_NAME            "JK_BD4A24S10P"

// ST7789 Display pins (adjust for your wiring)
#define TFT_MISO            19
#define TFT_MOSI            23
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             17
#define TFT_BL              4   // Backlight pin

// Display dimensions (landscape)
#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       240

// BLE Service/Characteristic UUIDs
#define SERVICE_UUID        "ffe0"
#define CHAR_UUID           "ffe1"

// Command bytes
#define CMD_DEVICE_INFO     0x97
#define CMD_CELL_INFO       0x96

// ============== COMBAT STYLE COLORS (16-bit RGB565) ==============
// Background - deep black with blue tint
#define COLOR_BG            0x0000  // Pure black base
#define COLOR_BG_DARK       0x0808  // Very dark gray
#define COLOR_BG_PANEL      0x1082  // Dark blue-gray

// Neon accent colors
#define COLOR_NEON_BLUE     0x07FF  // Cyan neon
#define COLOR_NEON_GREEN    0x07E0  // Green neon
#define COLOR_NEON_YELLOW   0xFFE0  // Yellow neon
#define COLOR_NEON_ORANGE   0xFC00  // Orange neon
#define COLOR_NEON_RED      0xF800  // Red neon
#define COLOR_NEON_PURPLE   0xF81F  // Purple neon
#define COLOR_NEON_WHITE    0xFFFF  // White

// Combat theme colors
#define COLOR_COMBAT_DARK   0x2104  // Dark panel
#define COLOR_COMBAT_MID    0x4208  // Mid panel
#define COLOR_COMBAT_LIGHT  0x632C  // Light panel
#define COLOR_BORDER        0x3186  // Border color
#define COLOR_BORDER_GLOW   0x4A69  // Glowing border

// Text colors
#define COLOR_TEXT_WHITE    0xFFFF
#define COLOR_TEXT_GRAY     0x8410
#define COLOR_TEXT_DIM      0x4208

// ============== GLOBAL OBJECTS ==============
TFT_eSPI tft = TFT_eSPI();

// ============== BMS DATA STRUCTURE ==============
struct BMSData {
  bool connected = false;
  uint32_t lastUpdate = 0;

  float cellVoltage[32] = {0};
  float cellResistance[32] = {0};
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  uint8_t maxCellNum = 0;
  uint8_t minCellNum = 0;

  float powerTubeTemp = 0;
  float batteryVoltage = 0;
  float batteryPower = 0;
  float chargeCurrent = 0;
  float tempSensor1 = 0;
  float tempSensor2 = 0;
  uint16_t errorsBitmask = 0;
  float balanceCurrent = 0;
  uint8_t balancingAction = 0;
  uint8_t soc = 0;
  float capacityRemain = 0;
  float nominalCapacity = 0;
  uint32_t cycleCount = 0;
  float cycleCapacity = 0;
  uint8_t soh = 0;
  uint32_t totalRuntime = 0;
  bool chargeMosfet = false;
  bool dischargeMosfet = false;
  bool balancing = false;

  char vendorID[17] = {0};
  char hardwareVersion[9] = {0};
  char softwareVersion[9] = {0};
  char deviceName[17] = {0};
  uint32_t deviceUptime = 0;

  float cellCount = 0;
  float totalBatteryCapacity = 0;
} bmsData;

// ============== BLE VARIABLES ==============
static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pRemoteChar = nullptr;
static bool doConnect = false;
static bool connected = false;
static BLEAdvertisedDevice* myDevice = nullptr;

// Frame assembly
uint8_t rxBuffer[320];
int rxIndex = 0;
bool frameStarted = false;
bool newDataAvailable = false;
uint8_t sequenceCounter = 0;

// ============== UI STATE ==============
bool screenInitialized = false;
unsigned long lastScreenUpdate = 0;
#define SCREEN_UPDATE_INTERVAL  500

// Previous values for partial redraw
float prevPower = -999;
float prevCurrent = -999;
float prevVoltage = -999;
uint8_t prevSOC = 255;
float prevTemp = -999;
bool prevConnected = false;

// ============== FUNCTION DECLARATIONS ==============
void setupDisplay();
void drawUI();
void drawCombatBackground();
void drawHeader();
void drawPowerCard();
void drawVoltageCurrentBar();
void drawCapacityCard();
void drawTempCard();
void drawBluetoothIcon(int x, int y, bool connected);
void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor);
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor);
uint16_t getPowerColor(float power);
uint16_t getTempColor(float temp);
void drawGlowText(int x, int y, const char* text, uint32_t color, uint32_t glowColor, int font);
void parseJK02_32S_Frame(uint8_t* data, uint16_t len);
uint8_t calculateCRC(const uint8_t* data, uint16_t len);
void sendCommand(uint8_t cmd);
void connectToBMS();

// ============== BLE CALLBACKS ==============
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    connected = true;
    bmsData.connected = true;
    Serial.println("BLE Connected to BMS");
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    bmsData.connected = false;
    doConnect = false;
    Serial.println("BLE Disconnected from BMS");
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.printf("Device found: %s [%s]\n",
                  advertisedDevice.getName().c_str(),
                  advertisedDevice.getAddress().toString().c_str());

    if (advertisedDevice.getAddress().toString() == BMS_MAC_ADDRESS) {
      Serial.println("Target BMS found!");
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

static void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  for (size_t i = 0; i < length; i++) {
    if (!frameStarted && i < length - 3 &&
        pData[i] == 0x55 && pData[i+1] == 0xAA &&
        pData[i+2] == 0xEB && pData[i+3] == 0x90) {
      frameStarted = true;
      rxIndex = 0;
      for (size_t j = i; j < length && rxIndex < 320; j++) {
        rxBuffer[rxIndex++] = pData[j];
      }
      break;
    } else if (frameStarted && rxIndex < 320) {
      rxBuffer[rxIndex++] = pData[i];
    }
  }

  if (frameStarted && rxIndex >= 300) {
    uint8_t crc = calculateCRC(rxBuffer, 299);
    if (crc == rxBuffer[299]) {
      parseJK02_32S_Frame(rxBuffer, rxIndex);
      newDataAvailable = true;
      bmsData.lastUpdate = millis();
    } else {
      Serial.println("CRC mismatch!");
    }
    frameStarted = false;
    rxIndex = 0;
  }
}

// ============== SETUP ==============
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("JK-BMS Combat Monitor Starting...");

  setupDisplay();

  BLEDevice::init("ESP32_JK_BMS_Monitor");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(30, false);

  Serial.println("Scanning for BMS...");
}

// ============== MAIN LOOP ==============
void loop() {
  if (doConnect) {
    connectToBMS();
    doConnect = false;
  }

  if (!connected && !doConnect && millis() - bmsData.lastUpdate > 10000) {
    Serial.println("Attempting reconnect...");
    BLEDevice::getScan()->start(10, false);
    bmsData.lastUpdate = millis();
  }

  if (millis() - lastScreenUpdate > SCREEN_UPDATE_INTERVAL) {
    drawUI();
    lastScreenUpdate = millis();
  }

  delay(10);
}

// ============== DISPLAY SETUP ==============
void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  screenInitialized = true;
}

// ============== COMBAT STYLE COLOR FUNCTIONS ==============
uint16_t getPowerColor(float power) {
  // Negative power (charging) = green
  if (power < 0) {
    return COLOR_NEON_GREEN;
  }
  // Positive power (discharging) = gradient from yellow to red based on power
  float maxPower = 5000.0; // 5kW max for full red
  float ratio = power / maxPower;
  if (ratio > 1.0) ratio = 1.0;
  if (ratio < 0.0) ratio = 0.0;

  if (ratio < 0.33) {
    // Yellow to Orange
    float localRatio = ratio / 0.33;
    uint8_t r = 0xFF;
    uint8_t g = 0xE0 + (0x00 - 0xE0) * localRatio;
    uint8_t b = 0x00;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  } else if (ratio < 0.66) {
    // Orange to Red-Orange
    float localRatio = (ratio - 0.33) / 0.33;
    uint8_t r = 0xFF;
    uint8_t g = 0x80 * (1.0 - localRatio);
    uint8_t b = 0x00;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  } else {
    // Red-Orange to Red
    float localRatio = (ratio - 0.66) / 0.34;
    uint8_t r = 0xFF;
    uint8_t g = 0x40 * (1.0 - localRatio);
    uint8_t b = 0x00;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
}

uint16_t getTempColor(float temp) {
  if (temp < 20) return COLOR_NEON_GREEN;
  if (temp < 35) return COLOR_NEON_YELLOW;
  if (temp < 45) return COLOR_NEON_ORANGE;
  return COLOR_NEON_RED;
}

// ============== DRAW HELPERS ==============
void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor) {
  tft.fillRoundRect(x, y, w, h, r, fillColor);
  tft.drawRoundRect(x, y, w, h, r, borderColor);
}

void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor) {
  tft.fillRoundRect(x, y, w, h, r, bgColor);
  int fillW = (int)((w - 4) * (percent / 100.0));
  if (fillW > 0) {
    if (fillW > w - 4) fillW = w - 4;
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, r - 2, barColor);
  }
}

void drawGlowText(int x, int y, const char* text, uint32_t color, uint32_t glowColor, int font) {
  // Draw glow effect (multiple offset texts)
  tft.setTextColor(glowColor, COLOR_BG);
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx != 0 || dy != 0) {
        tft.drawString(text, x + dx, y + dy, font);
      }
    }
  }
  // Draw main text
  tft.setTextColor(color, COLOR_BG);
  tft.drawString(text, x, y, font);
}

void drawBluetoothIcon(int x, int y, bool isConnected) {
  uint32_t color = isConnected ? COLOR_NEON_GREEN : COLOR_NEON_RED;
  // BT symbol
  tft.drawLine(x + 4, y, x + 4, y + 14, color);
  tft.drawLine(x + 4, y, x + 9, y + 4, color);
  tft.drawLine(x + 9, y + 4, x, y + 10, color);
  tft.drawLine(x, y + 4, x + 9, y + 10, color);
  tft.drawLine(x + 9, y + 10, x + 4, y + 14, color);
  if (isConnected) {
    tft.fillCircle(x + 12, y + 7, 2, COLOR_NEON_GREEN);
  }
}

// ============== DRAW COMBAT BACKGROUND ==============
void drawCombatBackground() {
  static bool bgDrawn = false;
  if (bgDrawn) return;

  tft.fillScreen(COLOR_BG);

  // Draw combat grid lines
  uint32_t gridColor = COLOR_COMBAT_MID;
  for (int i = 0; i < SCREEN_WIDTH; i += 40) {
    tft.drawLine(i, 30, i, SCREEN_HEIGHT, gridColor);
  }
  for (int i = 30; i < SCREEN_HEIGHT; i += 30) {
    tft.drawLine(0, i, SCREEN_WIDTH, i, gridColor);
  }

  // Top accent bar
  tft.fillRect(0, 0, SCREEN_WIDTH, 3, COLOR_NEON_BLUE);

  bgDrawn = true;
}

// ============== DRAW HEADER ==============
void drawHeader() {
  // Title with combat style
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_NEON_BLUE, COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("JK-BMS COMBAT", 12, 8, 2);

  // Bluetooth status
  drawBluetoothIcon(SCREEN_WIDTH - 50, 8, bmsData.connected);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(bmsData.connected ? COLOR_NEON_GREEN : COLOR_NEON_RED, COLOR_BG);
  tft.drawString(bmsData.connected ? "ONLINE" : "OFFLINE", SCREEN_WIDTH - 12, 10, 1);
}

// ============== DRAW POWER CARD (COMBAT STYLE) ==============
void drawPowerCard() {
  int x = 10, y = 32, w = SCREEN_WIDTH - 20, h = 95, r = 8;

  // Combat panel background
  drawRoundedRect(x, y, w, h, r, COLOR_COMBAT_DARK, COLOR_BORDER);

  // Inner glow border
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, COLOR_BORDER_GLOW);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  tft.drawString("实时功率", x + 12, y + 8, 1);

  // Power value with dynamic color
  float power = bmsData.batteryPower;
  uint16_t powerColor = getPowerColor(power);

  tft.setTextDatum(MC_DATUM);

  // Glow effect for power text
  char powerStr[16];
  if (fabs(power) >= 1000) {
    snprintf(powerStr, sizeof(powerStr), "%.2f", fabs(power) / 1000.0);
  } else {
    snprintf(powerStr, sizeof(powerStr), "%.0f", fabs(power));
  }

  // Draw glow
  tft.setTextColor(COLOR_COMBAT_MID, COLOR_COMBAT_DARK);
  for (int dx = -2; dx <= 2; dx++) {
    for (int dy = -2; dy <= 2; dy++) {
      if (dx != 0 || dy != 0) {
        tft.drawString(powerStr, x + w / 2 + dx, y + h / 2 + 2 + dy, 6);
      }
    }
  }

  // Draw main power text
  tft.setTextColor(powerColor, COLOR_COMBAT_DARK);
  tft.drawString(powerStr, x + w / 2, y + h / 2 + 2, 6);

  // Unit
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  if (fabs(power) >= 1000) {
    tft.drawString("kW", x + w / 2 + 80, y + h / 2 + 2, 2);
  } else {
    tft.drawString("W", x + w / 2 + 70, y + h / 2 + 2, 2);
  }

  // Power bar at bottom of card
  float powerPercent = fabs(power) / 5000.0 * 100;
  if (powerPercent > 100) powerPercent = 100;
  drawProgressBar(x + 12, y + h - 14, w - 24, 6, 3, powerPercent, powerColor, COLOR_COMBAT_MID);
}

// ============== DRAW VOLTAGE/CURRENT BAR ==============
void drawVoltageCurrentBar() {
  int x = 10, y = 132, w = SCREEN_WIDTH - 20, h = 38, r = 6;

  drawRoundedRect(x, y, w, h, r, COLOR_COMBAT_DARK, COLOR_BORDER);

  // Voltage (left)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_NEON_PURPLE, COLOR_COMBAT_DARK);
  char voltStr[16];
  snprintf(voltStr, sizeof(voltStr), "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, x + 12, y + 14, 2);

  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  tft.drawString("总电压", x + 12, y + 4, 1);

  // Current (right) - with combat color
  float current = bmsData.chargeCurrent;
  uint16_t currentColor = getPowerColor(current * -100); // Invert for current display

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(currentColor, COLOR_COMBAT_DARK);
  char currStr[16];
  snprintf(currStr, sizeof(currStr), "%.2fA", current);
  tft.drawString(currStr, x + w - 12, y + 14, 2);

  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  tft.drawString("电流", x + w - 12, y + 4, 1);

  // Center divider with arrow
  int cx = x + w / 2;
  int cy = y + h / 2;
  tft.drawLine(cx, y + 6, cx, y + h - 6, COLOR_BORDER);

  if (current > 0.1) {
    tft.fillTriangle(cx - 3, cy + 3, cx + 3, cy + 3, cx, cy - 4, COLOR_NEON_GREEN);
  } else if (current < -0.1) {
    tft.fillTriangle(cx - 3, cy - 3, cx + 3, cy - 3, cx, cy + 4, COLOR_NEON_RED);
  } else {
    tft.fillCircle(cx, cy, 2, COLOR_TEXT_GRAY);
  }
}

// ============== DRAW CAPACITY CARD ==============
void drawCapacityCard() {
  int x = 10, y = 175, w = 150, h = 58, r = 6;

  drawRoundedRect(x, y, w, h, r, COLOR_COMBAT_DARK, COLOR_BORDER);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  tft.drawString("电池容量", x + 10, y + 5, 1);

  // SOC large
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_NEON_GREEN, COLOR_COMBAT_DARK);
  char socStr[8];
  snprintf(socStr, sizeof(socStr), "%d%%", bmsData.soc);
  tft.drawString(socStr, x + 10, y + 18, 2);

  // Used/Total
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  char capStr[24];
  snprintf(capStr, sizeof(capStr), "%.1f/%.0fAh",
           bmsData.capacityRemain, bmsData.nominalCapacity);
  tft.drawString(capStr, x + w - 10, y + 24, 1);

  // SOC bar
  drawProgressBar(x + 10, y + 42, w - 20, 8, 3, bmsData.soc, COLOR_NEON_GREEN, COLOR_COMBAT_MID);
}

// ============== DRAW TEMPERATURE CARD ==============
void drawTempCard() {
  int x = 170, y = 175, w = 140, h = 58, r = 6;

  drawRoundedRect(x, y, w, h, r, COLOR_COMBAT_DARK, COLOR_BORDER);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  tft.drawString("电池温度", x + 10, y + 5, 1);

  // Temperature with color based on value
  float avgTemp = (bmsData.tempSensor1 + bmsData.tempSensor2) / 2.0;
  uint16_t tempColor = getTempColor(avgTemp);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tempColor, COLOR_COMBAT_DARK);
  char tempStr[24];
  if (bmsData.tempSensor1 == 0 && bmsData.tempSensor2 == 0) {
    snprintf(tempStr, sizeof(tempStr), "-- C");
  } else {
    snprintf(tempStr, sizeof(tempStr), "%.1fC", avgTemp);
  }
  tft.drawString(tempStr, x + 10, y + 18, 2);

  // MOS temp
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_GRAY, COLOR_COMBAT_DARK);
  char mosStr[16];
  snprintf(mosStr, sizeof(mosStr), "MOS:%.0fC", bmsData.powerTubeTemp);
  tft.drawString(mosStr, x + w - 10, y + 24, 1);

  // Temp bar
  float tempPercent = (avgTemp + 20) / 80.0 * 100;
  if (tempPercent < 0) tempPercent = 0;
  if (tempPercent > 100) tempPercent = 100;
  drawProgressBar(x + 10, y + 42, w - 20, 8, 3, tempPercent, tempColor, COLOR_COMBAT_MID);
}

// ============== DRAW MAIN UI ==============
void drawUI() {
  if (!screenInitialized) return;

  static bool firstDraw = true;
  if (firstDraw) {
    drawCombatBackground();
    firstDraw = false;
  }

  drawHeader();
  drawPowerCard();
  drawCapacityCard();
  drawTempCard();
  drawVoltageCurrentBar();
}

// ============== PARSE JK02_32S FRAME ==============
void parseJK02_32S_Frame(uint8_t* data, uint16_t len) {
  if (len < 300) return;

  uint8_t frameType = data[4];

  switch (frameType) {
    case 0x02: {
      for (int i = 0; i < 32; i++) {
        int offset = 6 + i * 2;
        bmsData.cellVoltage[i] = (data[offset] | (data[offset + 1] << 8)) * 0.001;
      }

      bmsData.avgCellVoltage = (data[74] | (data[75] << 8)) * 0.001;
      bmsData.deltaCellVoltage = (data[76] | (data[77] << 8)) * 0.001;
      bmsData.maxCellNum = data[78];
      bmsData.minCellNum = data[79];

      for (int i = 0; i < 32; i++) {
        int offset = 80 + i * 2;
        bmsData.cellResistance[i] = (data[offset] | (data[offset + 1] << 8)) * 0.001;
      }

      bmsData.powerTubeTemp = (int16_t)(data[144] | (data[145] << 8)) * 0.1;
      bmsData.batteryVoltage = ((uint32_t)data[150] | ((uint32_t)data[151] << 8) |
                                ((uint32_t)data[152] << 16) | ((uint32_t)data[153] << 24)) * 0.001;
      bmsData.batteryPower = ((uint32_t)data[154] | ((uint32_t)data[155] << 8) |
                              ((uint32_t)data[156] << 16) | ((uint32_t)data[157] << 24)) * 0.001;
      bmsData.chargeCurrent = (int32_t)((uint32_t)data[158] | ((uint32_t)data[159] << 8) |
                                        ((uint32_t)data[160] << 16) | ((uint32_t)data[161] << 24)) * 0.001;
      bmsData.tempSensor1 = (int16_t)(data[162] | (data[163] << 8)) * 0.1;
      bmsData.tempSensor2 = (int16_t)(data[164] | (data[165] << 8)) * 0.1;
      bmsData.errorsBitmask = data[166] | (data[167] << 8);
      bmsData.balanceCurrent = (int16_t)(data[170] | (data[171] << 8)) * 0.001;
      bmsData.balancingAction = data[172];
      bmsData.soc = data[173];
      bmsData.capacityRemain = ((uint32_t)data[174] | ((uint32_t)data[175] << 8) |
                                ((uint32_t)data[176] << 16) | ((uint32_t)data[177] << 24)) * 0.001;
      bmsData.nominalCapacity = ((uint32_t)data[178] | ((uint32_t)data[179] << 8) |
                                 ((uint32_t)data[180] << 16) | ((uint32_t)data[181] << 24)) * 0.001;
      bmsData.cycleCount = ((uint32_t)data[182] | ((uint32_t)data[183] << 8) |
                            ((uint32_t)data[184] << 16) | ((uint32_t)data[185] << 24));
      bmsData.cycleCapacity = ((uint32_t)data[186] | ((uint32_t)data[187] << 8) |
                               ((uint32_t)data[188] << 16) | ((uint32_t)data[189] << 24)) * 0.001;
      bmsData.soh = data[190];
      bmsData.totalRuntime = ((uint32_t)data[194] | ((uint32_t)data[195] << 8) |
                              ((uint32_t)data[196] << 16) | ((uint32_t)data[197] << 24));
      bmsData.chargeMosfet = data[198] != 0;
      bmsData.dischargeMosfet = data[199] != 0;
      bmsData.balancing = data[201] != 0;

      Serial.printf("[BMS] V:%.2fV I:%.2fA P:%.1fW SOC:%d%% T1:%.1fC T2:%.1fC MOS:%.1fC\n",
                    bmsData.batteryVoltage, bmsData.chargeCurrent, bmsData.batteryPower,
                    bmsData.soc, bmsData.tempSensor1, bmsData.tempSensor2, bmsData.powerTubeTemp);
      break;
    }

    case 0x03: {
      memcpy(bmsData.vendorID, &data[6], 16);
      bmsData.vendorID[16] = '\0';
      memcpy(bmsData.hardwareVersion, &data[22], 8);
      bmsData.hardwareVersion[8] = '\0';
      memcpy(bmsData.softwareVersion, &data[30], 8);
      bmsData.softwareVersion[8] = '\0';
      bmsData.deviceUptime = ((uint32_t)data[38] | ((uint32_t)data[39] << 8) |
                              ((uint32_t)data[40] << 16) | ((uint32_t)data[41] << 24));
      memcpy(bmsData.deviceName, &data[46], 16);
      bmsData.deviceName[16] = '\0';

      Serial.printf("[BMS] Device: %s HW:%s SW:%s\n",
                    bmsData.deviceName, bmsData.hardwareVersion, bmsData.softwareVersion);
      break;
    }

    case 0x01: {
      bmsData.cellCount = data[114];
      bmsData.totalBatteryCapacity = ((uint32_t)data[130] | ((uint32_t)data[131] << 8) |
                                      ((uint32_t)data[132] << 16) | ((uint32_t)data[133] << 24)) * 0.001;

      Serial.printf("[BMS] Settings: Cells:%.0f Capacity:%.0fAh\n",
                    bmsData.cellCount, bmsData.totalBatteryCapacity);
      break;
    }

    default:
      Serial.printf("Unknown frame type: 0x%02X\n", frameType);
      break;
  }
}

// ============== CRC CALCULATION ==============
uint8_t calculateCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) {
    crc += data[i];
  }
  return crc;
}

// ============== SEND COMMAND ==============
void sendCommand(uint8_t cmd) {
  uint8_t frame[20] = {
    0xAA, 0x55, 0x90, 0xEB,
    cmd,
    0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
    sequenceCounter++,
    0x00, 0x00,
    0x00
  };

  frame[19] = calculateCRC(frame, 19);

  if (pRemoteChar != nullptr) {
    pRemoteChar->writeValue(frame, 20);
    Serial.printf("Sent command: 0x%02X\n", cmd);
  }
}

// ============== CONNECT TO BMS ==============
void connectToBMS() {
  Serial.print("Connecting to BMS...");

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(myDevice)) {
    Serial.println("FAILED");
    connected = false;
    return;
  }

  Serial.println("Connected to BLE server");

  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Service not found!");
    pClient->disconnect();
    return;
  }

  pRemoteChar = pRemoteService->getCharacteristic(CHAR_UUID);
  if (pRemoteChar == nullptr) {
    Serial.println("Characteristic not found!");
    pClient->disconnect();
    return;
  }

  if (pRemoteChar->canNotify()) {
    pRemoteChar->registerForNotify(notifyCallback);
    Serial.println("Notifications registered");
  }

  delay(500);
  sendCommand(CMD_DEVICE_INFO);
  delay(500);
  sendCommand(CMD_CELL_INFO);

  connected = true;
  bmsData.connected = true;
  Serial.println("BMS ready - waiting for data...");
}
