/***************************************************
 * JK-BMS BLE Monitor for ESP32-32E + ST7789 2.8" TFT
 * Protocol: JK02_32S
 * BMS MAC: 98:da:20:07:b9:00
 * BMS Name: JK_BD4A24S10P
 * Display: 320x240 landscape (ST7789)
 * Style: Battle Mode / Sport Mode
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

// ============== BATTLE MODE COLORS ==============
// Background - Deep dark
#define COLOR_BG            0x000000  // Pure black
#define COLOR_CARD_BG       0x0A0A0A  // Near black card
#define COLOR_CARD_BORDER   0x1A1A1A  // Dark border

// Power colors - Dynamic based on power level
#define COLOR_POWER_LOW     0x00FF00  // Green (low power)
#define COLOR_POWER_MED     0xFFFF00  // Yellow (medium)
#define COLOR_POWER_HIGH    0xFF8800  // Orange (high)
#define COLOR_POWER_EXTREME 0xFF0000  // Red (extreme)
#define COLOR_POWER_BG      0x0A0000  // Dark red bg

// Current colors
#define COLOR_CHARGE        0x00FF41  // Bright green for charging (negative)
#define COLOR_DISCHARGE_LOW 0x00AAFF  // Cyan (low discharge)
#define COLOR_DISCHARGE_MED 0x0088FF  // Blue (med discharge)
#define COLOR_DISCHARGE_HIGH 0xFF0044 // Red (high discharge)

// Capacity - Electric blue
#define COLOR_CAPACITY      0x00E5FF  // Cyan
#define COLOR_CAPACITY_BG   0x00111A  // Dark cyan bg

// Temperature - Warning colors
#define COLOR_TEMP_COLD     0x00AAFF  // Blue (cold)
#define COLOR_TEMP_NORMAL   0x00FF41  // Green (normal)
#define COLOR_TEMP_WARM     0xFFAA00  // Orange (warm)
#define COLOR_TEMP_HOT      0xFF0044  // Red (hot)
#define COLOR_TEMP_BG       0x1A0A00  // Dark orange bg

// Voltage - Purple neon
#define COLOR_VOLTAGE       0xE040FB  // Purple

// Text
#define COLOR_TEXT_PRIMARY  0xFFFFFF  // White
#define COLOR_TEXT_SECOND   0x888888  // Gray
#define COLOR_TEXT_DIM      0x444444  // Dark gray

// Bluetooth
#define COLOR_BLUETOOTH_ON  0x00FF41  // Neon green
#define COLOR_BLUETOOTH_OFF 0xFF0044  // Neon red

// Battle mode accent
#define COLOR_ACCENT        0xFF0044  // Red accent
#define COLOR_GLOW          0x330011  // Glow effect color

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

uint8_t rxBuffer[320];
int rxIndex = 0;
bool frameStarted = false;
bool newDataAvailable = false;
uint8_t sequenceCounter = 0;

// ============== UI STATE ==============
bool screenInitialized = false;
unsigned long lastScreenUpdate = 0;
#define SCREEN_UPDATE_INTERVAL  500

// ============== FUNCTION DECLARATIONS ==============
void setupDisplay();
void drawUI();
void drawBattleCard(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor, uint32_t accentColor);
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor);
void drawBluetoothIcon(int x, int y, bool connected);
void drawPowerCard();
void drawCapacityCard();
void drawTempCard();
void drawVoltageCurrentBar();
uint32_t getPowerColor(float power);
uint32_t getCurrentColor(float current);
uint32_t getTempColor(float temp);
void drawGlowText(int x, int y, const char* text, uint32_t color, uint32_t glowColor, int textSize);
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
  Serial.println("JK-BMS Battle Mode Starting...");

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

// ============== BATTLE MODE CARD ==============
void drawBattleCard(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor, uint32_t accentColor) {
  // Outer glow
  tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, r + 1, COLOR_GLOW);
  
  // Fill
  tft.fillRoundRect(x, y, w, h, r, fillColor);
  
  // Border
  tft.drawRoundRect(x, y, w, h, r, borderColor);
  
  // Top accent line (battle mode style)
  tft.fillRoundRect(x + 8, y, w - 16, 2, 1, accentColor);
  
  // Corner accents
  tft.drawLine(x + 2, y + 4, x + 2, y + 2, accentColor);
  tft.drawLine(x + 4, y + 2, x + 2, y + 2, accentColor);
  tft.drawLine(x + w - 3, y + 4, x + w - 3, y + 2, accentColor);
  tft.drawLine(x + w - 5, y + 2, x + w - 3, y + 2, accentColor);
  tft.drawLine(x + 2, y + h - 5, x + 2, y + h - 3, accentColor);
  tft.drawLine(x + 4, y + h - 3, x + 2, y + h - 3, accentColor);
  tft.drawLine(x + w - 3, y + h - 5, x + w - 3, y + h - 3, accentColor);
  tft.drawLine(x + w - 5, y + h - 3, x + w - 3, y + h - 3, accentColor);
}

// ============== PROGRESS BAR ==============
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor) {
  tft.fillRoundRect(x, y, w, h, r, bgColor);
  int fillW = (int)((w - 4) * (percent / 100.0));
  if (fillW > 0) {
    if (fillW > w - 4) fillW = w - 4;
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, r - 2, barColor);
    // Glow on progress bar
    if (fillW > 4) {
      tft.drawLine(x + 2, y + 2, x + fillW, y + 2, barColor);
    }
  }
}

// ============== BLUETOOTH ICON ==============
void drawBluetoothIcon(int x, int y, bool isConnected) {
  uint32_t color = isConnected ? COLOR_BLUETOOTH_ON : COLOR_BLUETOOTH_OFF;
  
  // Battle mode BT icon - sharper, more aggressive
  tft.drawLine(x + 3, y, x + 3, y + 12, color);
  tft.drawLine(x + 3, y, x + 8, y + 4, color);
  tft.drawLine(x + 8, y + 4, x, y + 8, color);
  tft.drawLine(x, y + 4, x + 8, y + 8, color);
  tft.drawLine(x + 8, y + 8, x + 3, y + 12, color);
  
  if (isConnected) {
    tft.fillCircle(x + 11, y + 6, 2, COLOR_BLUETOOTH_ON);
    // Pulse ring
    tft.drawCircle(x + 11, y + 6, 4, COLOR_BLUETOOTH_ON);
  }
}

// ============== DYNAMIC COLOR FUNCTIONS ==============
uint32_t getPowerColor(float power) {
  float absPower = abs(power);
  if (absPower < 100) return COLOR_POWER_LOW;
  if (absPower < 500) return COLOR_POWER_MED;
  if (absPower < 1000) return COLOR_POWER_HIGH;
  return COLOR_POWER_EXTREME;
}

uint32_t getCurrentColor(float current) {
  // Charging (negative) = green
  if (current < 0) {
    return COLOR_CHARGE;
  }
  // Discharging (positive) = dynamic based on current magnitude
  if (current < 1) return COLOR_DISCHARGE_LOW;
  if (current < 10) return COLOR_DISCHARGE_MED;
  return COLOR_DISCHARGE_HIGH;
}

uint32_t getTempColor(float temp) {
  if (temp < 10) return COLOR_TEMP_COLD;
  if (temp < 35) return COLOR_TEMP_NORMAL;
  if (temp < 50) return COLOR_TEMP_WARM;
  return COLOR_TEMP_HOT;
}

// ============== GLOW TEXT ==============
void drawGlowText(int x, int y, const char* text, uint32_t color, uint32_t glowColor, int textSize) {
  // Draw glow layers
  tft.setTextColor(glowColor, COLOR_BG);
  tft.drawString(text, x - 1, y, textSize);
  tft.drawString(text, x + 1, y, textSize);
  tft.drawString(text, x, y - 1, textSize);
  tft.drawString(text, x, y + 1, textSize);
  
  // Main text
  tft.setTextColor(color, COLOR_BG);
  tft.drawString(text, x, y, textSize);
}

// ============== MAIN UI ==============
void drawUI() {
  if (!screenInitialized) return;

  static bool firstDraw = true;
  if (firstDraw) {
    tft.fillScreen(COLOR_BG);
    firstDraw = false;
  }

  // === HEADER ===
  // Title - Battle mode style
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("JK-BMS ", 12, 8, 2);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.drawString("BATTLE MODE", 80, 8, 2);

  // Bluetooth icon
  drawBluetoothIcon(SCREEN_WIDTH - 28, 8, bmsData.connected);

  // Connection status
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(bmsData.connected ? COLOR_BLUETOOTH_ON : COLOR_BLUETOOTH_OFF, COLOR_BG);
  tft.drawString(bmsData.connected ? "ONLINE" : "OFFLINE", SCREEN_WIDTH - 34, 10, 1);

  // === MAIN POWER CARD ===
  drawPowerCard();

  // === CAPACITY CARD ===
  drawCapacityCard();

  // === TEMPERATURE CARD ===
  drawTempCard();

  // === VOLTAGE/CURRENT BAR ===
  drawVoltageCurrentBar();
}

// ============== DRAW POWER CARD ==============
void drawPowerCard() {
  int x = 12, y = 32, w = SCREEN_WIDTH - 24, h = 90, r = 12;
  
  uint32_t powerColor = getPowerColor(bmsData.batteryPower);
  
  drawBattleCard(x, y, w, h, r, COLOR_POWER_BG, COLOR_CARD_BORDER, powerColor);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_POWER_BG);
  tft.drawString("实时功率", x + 15, y + 10, 1);
  
  // Power unit label
  tft.setTextDatum(TR_DATUM);
  tft.drawString("POWER", x + w - 15, y + 10, 1);

  // Power value (large, centered)
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(powerColor, COLOR_POWER_BG);

  float power = bmsData.batteryPower;
  char powerStr[16];
  if (abs(power) >= 1000) {
    snprintf(powerStr, sizeof(powerStr), "%.2f kW", abs(power) / 1000.0);
  } else {
    snprintf(powerStr, sizeof(powerStr), "%.0f W", abs(power));
  }
  tft.drawString(powerStr, x + w / 2, y + h / 2 + 5, 4);

  // Status indicator - simplified
  tft.setTextDatum(TC_DATUM);
  if (bmsData.chargeCurrent < -0.1) {
    tft.setTextColor(COLOR_CHARGE, COLOR_POWER_BG);
    tft.drawString("充电中", x + w / 2, y + h - 18, 1);
  } else if (bmsData.chargeCurrent > 0.1) {
    tft.setTextColor(powerColor, COLOR_POWER_BG);
    tft.drawString("放电中", x + w / 2, y + h - 18, 1);
  } else {
    tft.setTextColor(COLOR_TEXT_SECOND, COLOR_POWER_BG);
    tft.drawString("待机", x + w / 2, y + h - 18, 1);
  }
}

// ============== DRAW CAPACITY CARD ==============
void drawCapacityCard() {
  int x = 12, y = 172, w = 148, h = 60, r = 10;

  drawBattleCard(x, y, w, h, r, COLOR_CAPACITY_BG, COLOR_CARD_BORDER, COLOR_CAPACITY);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CAPACITY_BG);
  tft.drawString("电池容量", x + 10, y + 6, 1);

  // SOC percentage (large)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_CAPACITY, COLOR_CAPACITY_BG);
  char socStr[8];
  snprintf(socStr, sizeof(socStr), "%d%%", bmsData.soc);
  tft.drawString(socStr, x + 10, y + 22, 2);

  // Used/Total
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CAPACITY_BG);
  char capStr[24];
  snprintf(capStr, sizeof(capStr), "%.1f/%.0fAh",
           bmsData.capacityRemain, bmsData.nominalCapacity);
  tft.drawString(capStr, x + w - 10, y + 28, 1);

  // Progress bar
  drawProgressBar(x + 10, y + 44, w - 20, 8, 4, bmsData.soc, COLOR_CAPACITY, 0x001111);
}

// ============== DRAW TEMPERATURE CARD ==============
void drawTempCard() {
  int x = 172, y = 172, w = 136, h = 60, r = 10;

  float avgTemp = (bmsData.tempSensor1 + bmsData.tempSensor2) / 2.0;
  uint32_t tempColor = getTempColor(avgTemp);

  drawBattleCard(x, y, w, h, r, COLOR_TEMP_BG, COLOR_CARD_BORDER, tempColor);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_TEMP_BG);
  tft.drawString("电池温度", x + 10, y + 6, 1);

  // Temperature values
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tempColor, COLOR_TEMP_BG);
  char tempStr[24];

  if (bmsData.tempSensor1 == 0 && bmsData.tempSensor2 == 0) {
    snprintf(tempStr, sizeof(tempStr), "-- C");
  } else {
    snprintf(tempStr, sizeof(tempStr), "%.1f C", avgTemp);
  }
  tft.drawString(tempStr, x + 10, y + 22, 2);

  // MOS temp
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_TEMP_BG);
  char mosStr[16];
  snprintf(mosStr, sizeof(mosStr), "MOS:%.0fC", bmsData.powerTubeTemp);
  tft.drawString(mosStr, x + w - 10, y + 28, 1);

  // Temp bar
  float tempPercent = (avgTemp + 20) / 80.0 * 100;
  if (tempPercent < 0) tempPercent = 0;
  if (tempPercent > 100) tempPercent = 100;
  drawProgressBar(x + 10, y + 44, w - 20, 8, 4, tempPercent, tempColor, 0x111100);
}

// ============== DRAW VOLTAGE/CURRENT BAR ==============
void drawVoltageCurrentBar() {
  int x = 12, y = 130, w = SCREEN_WIDTH - 24, h = 34, r = 8;

  drawBattleCard(x, y, w, h, r, COLOR_CARD_BG, COLOR_CARD_BORDER, COLOR_VOLTAGE);

  // Voltage (left side)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_VOLTAGE, COLOR_CARD_BG);
  char voltStr[16];
  snprintf(voltStr, sizeof(voltStr), "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, x + 12, y + 10, 2);

  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("总电压", x + 12, y + 2, 1);

  // Current (right side) - Dynamic color based on current
  uint32_t currentColor = getCurrentColor(bmsData.chargeCurrent);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(currentColor, COLOR_CARD_BG);
  char currStr[16];
  snprintf(currStr, sizeof(currStr), "%.2fA", bmsData.chargeCurrent);
  tft.drawString(currStr, x + w - 12, y + 10, 2);

  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("电流", x + w - 12, y + 2, 1);

  // Center divider - battle style
  tft.drawLine(x + w / 2, y + 6, x + w / 2, y + h - 6, COLOR_CARD_BORDER);
  
  // Direction indicator with dynamic color
  int cx = x + w / 2;
  int cy = y + h / 2;
  if (bmsData.chargeCurrent < -0.1) {
    // Charging arrow up - green
    tft.fillTriangle(cx - 4, cy + 3, cx + 4, cy + 3, cx, cy - 5, COLOR_CHARGE);
  } else if (bmsData.chargeCurrent > 0.1) {
    // Discharging arrow down - dynamic color
    tft.fillTriangle(cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 5, currentColor);
  } else {
    tft.fillCircle(cx, cy, 3, COLOR_TEXT_DIM);
  }
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
