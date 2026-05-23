/***************************************************
 * JK-BMS BLE Monitor for ESP32-32E + ST7789 2.8" TFT
 * Protocol: JK02_32S
 * BMS MAC: 98:da:20:07:b9:00
 * BMS Name: JK_BD4A24S10P
 * Display: 320x240 landscape (ST7789)
 * Style: SPORT MODE / Battle Style
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

// ============== BATTLE STYLE COLORS (16-bit RGB565) ==============
// Background
#define COLOR_BG            0x0505    // Deep black
#define COLOR_BG_DARK       0x0202    // Darker black

// Power - dynamic based on intensity
#define COLOR_POWER_LOW     0x07E0    // Green
#define COLOR_POWER_MID     0xFFE0    // Yellow
#define COLOR_POWER_HIGH    0xFD20    // Orange
#define COLOR_POWER_MAX     0xF800    // Red
#define COLOR_POWER_GLOW    0x7800    // Dark red glow

// Cards
#define COLOR_CARD_BORDER   0x3800    // Dark red border
#define COLOR_CARD_GLOW     0x4800    // Red glow

// Voltage / Current
#define COLOR_VOLTAGE       0xFEA0    // Amber/Gold
#define COLOR_CURRENT_DISCHARGE 0x07E0  // Green for discharge
#define COLOR_CURRENT_IDLE  0x8410    // Gray

// Capacity
#define COLOR_CAPACITY      0x07E0    // Green
#define COLOR_CAPACITY_GLOW 0x03E0    // Dark green glow

// Temperature
#define COLOR_TEMP          0xFD20    // Orange
#define COLOR_TEMP_GLOW     0xA000    // Dark orange glow

// Text
#define COLOR_TEXT_PRIMARY  0xFFFF    // White
#define COLOR_TEXT_SECOND   0xC618    // Light gray
#define COLOR_TEXT_DIM      0x7BEF    // Dim gray

// Bluetooth
#define COLOR_BLUETOOTH_ON  0x07E0    // Green
#define COLOR_BLUETOOTH_OFF 0xF800    // Red

// Progress bar bg
#define COLOR_PROGRESS_BG   0x1082    // Dark gray

// Corner decoration
#define COLOR_CORNER        0x7800    // Dark red

// ============== GLOBAL OBJECTS ==============
TFT_eSPI tft = TFT_eSPI();

// ============== BMS DATA STRUCTURE ==============
struct BMSData {
  bool connected = false;
  uint32_t lastUpdate = 0;
  float batteryVoltage = 0;
  float batteryPower = 0;
  float chargeCurrent = 0;
  float tempSensor1 = 0;
  float tempSensor2 = 0;
  float powerTubeTemp = 0;
  uint8_t soc = 0;
  float capacityRemain = 0;
  float nominalCapacity = 0;
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
uint8_t sequenceCounter = 0;

// ============== UI STATE ==============
bool screenInitialized = false;
unsigned long lastScreenUpdate = 0;
#define SCREEN_UPDATE_INTERVAL  500

// Animation
uint8_t scanLineY = 0;
unsigned long lastScanTime = 0;

// ============== FUNCTION DECLARATIONS ==============
void setupDisplay();
void drawUI();
void drawCornerDecorations();
void drawHeader();
void drawPowerCard();
void drawPowerBar();
void drawVoltageCurrentBar();
void drawCapacityCard();
void drawTempCard();
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint16_t barColor);

uint16_t getPowerColor(float power);
uint16_t getPowerBarColor(float power);
uint16_t getCurrentColor(float current, float power);

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
  Serial.println("JK-BMS SPORT MODE Starting...");

  setupDisplay();

  BLEDevice::init("ESP32_JK_BMS_SPORT");
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

// ============== COLOR HELPERS ==============
uint16_t getPowerColor(float power) {
  float ratio = constrain(power / 3000.0, 0.0, 1.0);
  if (ratio < 0.3f) return COLOR_POWER_LOW;
  if (ratio < 0.6f) {
    // Green -> Yellow
    uint8_t r = (uint8_t)(255 * ((ratio - 0.3f) / 0.3f));
    return tft.color565(r, 255, 0);
  }
  if (ratio < 0.8f) {
    // Yellow -> Orange
    uint8_t g = (uint8_t)(255 - (255 - 128) * ((ratio - 0.6f) / 0.2f));
    return tft.color565(255, g, 0);
  }
  // Orange -> Red
  uint8_t g = (uint8_t)(128 * (1.0f - (ratio - 0.8f) / 0.2f));
  return tft.color565(255, g, 0);
}

uint16_t getPowerBarColor(float power) {
  float ratio = constrain(power / 3000.0, 0.0, 1.0);
  if (ratio < 0.3f) return COLOR_POWER_LOW;
  if (ratio < 0.6f) return COLOR_POWER_MID;
  if (ratio < 0.8f) return COLOR_POWER_HIGH;
  return COLOR_POWER_MAX;
}

uint16_t getCurrentColor(float current, float power) {
  if (current < -0.1f) {
    return COLOR_CURRENT_DISCHARGE;  // Discharge = Green
  } else if (current > 0.1f) {
    return getPowerColor(power);      // Charge = Power color
  }
  return COLOR_CURRENT_IDLE;
}

// ============== DRAW UI ==============
void drawUI() {
  if (!screenInitialized) return;

  static bool firstDraw = true;
  if (firstDraw) {
    tft.fillScreen(COLOR_BG);
    firstDraw = false;
  }

  drawCornerDecorations();
  drawHeader();
  drawPowerCard();
  drawPowerBar();
  drawVoltageCurrentBar();
  drawCapacityCard();
  drawTempCard();
}

// ============== CORNER DECORATIONS ==============
void drawCornerDecorations() {
  int len = 15;
  int thick = 2;
  uint16_t c = COLOR_CORNER;

  // Top-left
  tft.fillRect(5, 5, len, thick, c);
  tft.fillRect(5, 5, thick, len, c);
  // Top-right
  tft.fillRect(SCREEN_WIDTH - 5 - len, 5, len, thick, c);
  tft.fillRect(SCREEN_WIDTH - 5 - thick, 5, thick, len, c);
  // Bottom-left
  tft.fillRect(5, SCREEN_HEIGHT - 5 - thick, len, thick, c);
  tft.fillRect(5, SCREEN_HEIGHT - 5 - len, thick, len, c);
  // Bottom-right
  tft.fillRect(SCREEN_WIDTH - 5 - len, SCREEN_HEIGHT - 5 - thick, len, thick, c);
  tft.fillRect(SCREEN_WIDTH - 5 - thick, SCREEN_HEIGHT - 5 - len, thick, len, c);
}

// ============== HEADER ==============
void drawHeader() {
  // Title
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tft.color565(255, 0, 64), COLOR_BG);
  tft.setTextSize(1);
  tft.drawString("JK-BMS MONITOR", 20, 6, 2);

  // SPORT MODE label
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(tft.color565(255, 0, 64), COLOR_BG);
  tft.drawString("SPORT MODE", SCREEN_WIDTH / 2, 6, 1);

  // Bluetooth status
  int bx = SCREEN_WIDTH - 55;
  int by = 6;
  if (bmsData.connected) {
    tft.setTextColor(COLOR_BLUETOOTH_ON, COLOR_BG);
    tft.drawString("ONLINE", bx + 15, by + 4, 1);
    // BT icon
    tft.fillCircle(bx, by + 6, 3, COLOR_BLUETOOTH_ON);
  } else {
    tft.setTextColor(COLOR_BLUETOOTH_OFF, COLOR_BG);
    tft.drawString("OFFLINE", bx + 15, by + 4, 1);
    tft.drawCircle(bx, by + 6, 3, COLOR_BLUETOOTH_OFF);
  }

  // Header divider line
  tft.drawLine(10, 24, SCREEN_WIDTH - 10, 24, tft.color565(255, 0, 64));
}

// ============== POWER CARD ==============
void drawPowerCard() {
  int x = 12, y = 30, w = SCREEN_WIDTH - 24, h = 90;

  // Card background with glow
  tft.fillRoundRect(x, y, w, h, 10, tft.color565(20, 0, 5));
  tft.drawRoundRect(x, y, w, h, 10, COLOR_CARD_BORDER);

  // Inner glow line
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, tft.color565(60, 10, 10));

  // Scan line animation (simulated)
  if (millis() - lastScanTime > 50) {
    scanLineY = (scanLineY + 1) % (h - 4);
    lastScanTime = millis();
  }
  int scanY = y + 2 + scanLineY;
  if (scanY < y + h - 2) {
    tft.drawLine(x + 2, scanY, x + w - 2, scanY, tft.color565(255, 0, 64));
  }

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(20, 0, 5));
  tft.drawString("SHI SHI GONGLV", x + 12, y + 8, 1);

  // Power value
  tft.setTextDatum(MC_DATUM);
  uint16_t pColor = getPowerColor(bmsData.batteryPower);
  tft.setTextColor(pColor, tft.color565(20, 0, 5));

  char powerStr[16];
  if (bmsData.batteryPower >= 1000) {
    snprintf(powerStr, sizeof(powerStr), "%.2f", bmsData.batteryPower / 1000.0);
    tft.drawString(powerStr, x + w / 2 - 15, y + h / 2 + 2, 4);
    tft.setTextColor(pColor, tft.color565(20, 0, 5));
    tft.drawString("kW", x + w / 2 + 45, y + h / 2 + 2, 2);
  } else {
    snprintf(powerStr, sizeof(powerStr), "%.0f", bmsData.batteryPower);
    tft.drawString(powerStr, x + w / 2 - 10, y + h / 2 + 2, 4);
    tft.setTextColor(pColor, tft.color565(20, 0, 5));
    tft.drawString("W", x + w / 2 + 40, y + h / 2 + 2, 2);
  }

  // Status text - Chinese
  tft.setTextDatum(TC_DATUM);
  if (bmsData.chargeCurrent > 0.1) {
    tft.setTextColor(getPowerColor(bmsData.batteryPower), tft.color565(20, 0, 5));
    tft.drawString("CHONG DIAN ZHONG", x + w / 2, y + h - 16, 1);
  } else if (bmsData.chargeCurrent < -0.1) {
    tft.setTextColor(COLOR_CAPACITY, tft.color565(20, 0, 5));
    tft.drawString("FANG DIAN ZHONG", x + w / 2, y + h - 16, 1);
  } else {
    tft.setTextColor(COLOR_TEXT_DIM, tft.color565(20, 0, 5));
    tft.drawString("DAI JI", x + w / 2, y + h - 16, 1);
  }
}

// ============== POWER BAR ==============
void drawPowerBar() {
  int x = 12, y = 124, w = SCREEN_WIDTH - 24, h = 8;

  // Background
  tft.fillRoundRect(x, y, w, h, 3, COLOR_PROGRESS_BG);

  // Fill
  float ratio = constrain(bmsData.batteryPower / 3000.0, 0.0, 1.0);
  int fillW = (int)((w - 2) * ratio);
  if (fillW > 0) {
    uint16_t barColor = getPowerBarColor(bmsData.batteryPower);
    tft.fillRoundRect(x + 1, y + 1, fillW, h - 2, 2, barColor);
    // Glow tip
    if (fillW > 5) {
      tft.fillRect(x + fillW - 3, y + 1, 3, h - 2, tft.color565(255, 255, 255));
    }
  }

  // Labels
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
  tft.drawString("0W", x, y + 10, 1);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("3000W", x + w, y + 10, 1);
}

// ============== VOLTAGE/CURRENT BAR ==============
void drawVoltageCurrentBar() {
  int x = 12, y = 148, w = SCREEN_WIDTH - 24, h = 32;

  // Background
  tft.fillRoundRect(x, y, w, h, 6, tft.color565(15, 5, 5));
  tft.drawRoundRect(x, y, w, h, 6, tft.color565(80, 30, 10));

  // Voltage (left)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(15, 5, 5));
  tft.drawString("ZONG DIAN YA", x + 10, y + 3, 1);

  tft.setTextColor(COLOR_VOLTAGE, tft.color565(15, 5, 5));
  char voltStr[16];
  snprintf(voltStr, sizeof(voltStr), "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, x + 10, y + 14, 2);

  // Current (right)
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(15, 5, 5));
  tft.drawString("DIAN LIU", x + w - 10, y + 3, 1);

  uint16_t cColor = getCurrentColor(bmsData.chargeCurrent, bmsData.batteryPower);
  tft.setTextColor(cColor, tft.color565(15, 5, 5));
  char currStr[16];
  snprintf(currStr, sizeof(currStr), "%.2fA", bmsData.chargeCurrent);
  tft.drawString(currStr, x + w - 10, y + 14, 2);

  // Center divider
  tft.drawLine(x + w / 2, y + 6, x + w / 2, y + h - 6, tft.color565(80, 30, 10));

  // Direction arrow
  int cx = x + w / 2;
  int cy = y + h / 2;
  if (bmsData.chargeCurrent > 0.1) {
    tft.fillTriangle(cx - 3, cy + 3, cx + 3, cy + 3, cx, cy - 4, getPowerColor(bmsData.batteryPower));
  } else if (bmsData.chargeCurrent < -0.1) {
    tft.fillTriangle(cx - 3, cy - 3, cx + 3, cy - 3, cx, cy + 4, COLOR_CURRENT_DISCHARGE);
  } else {
    tft.fillCircle(cx, cy, 2, COLOR_TEXT_DIM);
  }
}

// ============== CAPACITY CARD ==============
void drawCapacityCard() {
  int x = 12, y = 186, w = 148, h = 48;

  tft.fillRoundRect(x, y, w, h, 8, tft.color565(0, 20, 5));
  tft.drawRoundRect(x, y, w, h, 8, tft.color565(0, 100, 30));

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(0, 20, 5));
  tft.drawString("SHENG YU RONG LIANG", x + 8, y + 3, 1);

  tft.setTextColor(COLOR_CAPACITY, tft.color565(0, 20, 5));
  char socStr[8];
  snprintf(socStr, sizeof(socStr), "%d%%", bmsData.soc);
  tft.drawString(socStr, x + 8, y + 16, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(0, 20, 5));
  char capStr[24];
  snprintf(capStr, sizeof(capStr), "%.1f/%.0fAh",
           bmsData.capacityRemain, bmsData.nominalCapacity);
  tft.drawString(capStr, x + w - 8, y + 20, 1);

  drawProgressBar(x + 8, y + 36, w - 16, 6, 3, bmsData.soc, COLOR_CAPACITY);
}

// ============== TEMP CARD ==============
void drawTempCard() {
  int x = 168, y = 186, w = 140, h = 48;

  tft.fillRoundRect(x, y, w, h, 8, tft.color565(20, 8, 0));
  tft.drawRoundRect(x, y, w, h, 8, tft.color565(150, 60, 0));

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(20, 8, 0));
  tft.drawString("DIAN CHI WEN DU", x + 8, y + 3, 1);

  float avgTemp = (bmsData.tempSensor1 + bmsData.tempSensor2) / 2.0;
  tft.setTextColor(COLOR_TEMP, tft.color565(20, 8, 0));
  char tempStr[16];
  if (bmsData.tempSensor1 == 0 && bmsData.tempSensor2 == 0) {
    snprintf(tempStr, sizeof(tempStr), "-- C");
  } else {
    snprintf(tempStr, sizeof(tempStr), "%.1fC", avgTemp);
  }
  tft.drawString(tempStr, x + 8, y + 16, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_TEXT_DIM, tft.color565(20, 8, 0));
  char mosStr[16];
  snprintf(mosStr, sizeof(mosStr), "MOS:%.0fC", bmsData.powerTubeTemp);
  tft.drawString(mosStr, x + w - 8, y + 20, 1);

  float tempPercent = (avgTemp + 10) / 70.0 * 100;
  tempPercent = constrain(tempPercent, 0, 100);
  drawProgressBar(x + 8, y + 36, w - 16, 6, 3, tempPercent, COLOR_TEMP);
}

// ============== PROGRESS BAR ==============
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint16_t barColor) {
  tft.fillRoundRect(x, y, w, h, r, COLOR_PROGRESS_BG);
  int fillW = (int)((w - 2) * (percent / 100.0));
  if (fillW > 0) {
    if (fillW > w - 2) fillW = w - 2;
    tft.fillRoundRect(x + 1, y + 1, fillW, h - 2, r - 1, barColor);
    if (fillW > 4) {
      tft.fillRect(x + fillW - 2, y + 1, 2, h - 2, tft.color565(255, 255, 255));
    }
  }
}

// ============== PARSE FRAME ==============
void parseJK02_32S_Frame(uint8_t* data, uint16_t len) {
  if (len < 300) return;
  uint8_t frameType = data[4];

  switch (frameType) {
    case 0x02: {
      bmsData.powerTubeTemp = (int16_t)(data[144] | (data[145] << 8)) * 0.1;
      bmsData.batteryVoltage = ((uint32_t)data[150] | ((uint32_t)data[151] << 8) |
                                ((uint32_t)data[152] << 16) | ((uint32_t)data[153] << 24)) * 0.001;
      bmsData.batteryPower = ((uint32_t)data[154] | ((uint32_t)data[155] << 8) |
                              ((uint32_t)data[156] << 16) | ((uint32_t)data[157] << 24)) * 0.001;
      bmsData.chargeCurrent = (int32_t)((uint32_t)data[158] | ((uint32_t)data[159] << 8) |
                                        ((uint32_t)data[160] << 16) | ((uint32_t)data[161] << 24)) * 0.001;
      bmsData.tempSensor1 = (int16_t)(data[162] | (data[163] << 8)) * 0.1;
      bmsData.tempSensor2 = (int16_t)(data[164] | (data[165] << 8)) * 0.1;
      bmsData.soc = data[173];
      bmsData.capacityRemain = ((uint32_t)data[174] | ((uint32_t)data[175] << 8) |
                                ((uint32_t)data[176] << 16) | ((uint32_t)data[177] << 24)) * 0.001;
      bmsData.nominalCapacity = ((uint32_t)data[178] | ((uint32_t)data[179] << 8) |
                                 ((uint32_t)data[180] << 16) | ((uint32_t)data[181] << 24)) * 0.001;

      Serial.printf("[BMS] V:%.2fV I:%.2fA P:%.1fW SOC:%d%% T1:%.1fC T2:%.1fC MOS:%.1fC\n",
                    bmsData.batteryVoltage, bmsData.chargeCurrent, bmsData.batteryPower,
                    bmsData.soc, bmsData.tempSensor1, bmsData.tempSensor2, bmsData.powerTubeTemp);
      break;
    }
    case 0x03: {
      Serial.println("[BMS] Device info received");
      break;
    }
    case 0x01: {
      Serial.println("[BMS] Settings received");
      break;
    }
    default:
      Serial.printf("Unknown frame type: 0x%02X\n", frameType);
      break;
  }
}

// ============== CRC ==============
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
