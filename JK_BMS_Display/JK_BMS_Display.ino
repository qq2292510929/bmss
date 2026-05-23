/***************************************************
 * JK-BMS BLE Monitor for ESP32-32E + ST7789 2.8" TFT
 * Protocol: JK02_32S
 * BMS MAC: 98:da:20:07:b9:00
 * BMS Name: JK_BD4A24S10P
 * Display: 320x240 landscape (ST7789)
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

// Colors (16-bit RGB565)
#define COLOR_BG            0x0A0A2E  // Deep navy background
#define COLOR_CARD_BG       0x151540  // Slightly lighter card bg
#define COLOR_CARD_BORDER   0x2A2A6A  // Card border
#define COLOR_POWER         0x00E5FF  // Cyan for power
#define COLOR_POWER_BG      0x003344  // Dark cyan bg
#define COLOR_CAPACITY      0x00E676  // Green for capacity
#define COLOR_CAPACITY_BG   0x003311  // Dark green bg
#define COLOR_TEMP          0xFFAB40  // Orange for temp
#define COLOR_TEMP_BG       0x331100  // Dark orange bg
#define COLOR_VOLTAGE       0xE040FB  // Purple for voltage
#define COLOR_CURRENT       0xFF5252  // Red for current
#define COLOR_TEXT_PRIMARY  0xFFFFFF  // White
#define COLOR_TEXT_SECOND   0xB0B0D0  // Light gray-blue
#define COLOR_BLUETOOTH_ON  0x00E676  // Green BT connected
#define COLOR_BLUETOOTH_OFF 0xFF5252  // Red BT disconnected
#define COLOR_PROGRESS_BG   0x1A1A4E  // Progress bar background

// ============== GLOBAL OBJECTS ==============
TFT_eSPI tft = TFT_eSPI();

// ============== BMS DATA STRUCTURE ==============
struct BMSData {
  // Connection state
  bool connected = false;
  uint32_t lastUpdate = 0;

  // Cell info (Frame 0x02)
  float cellVoltage[32] = {0};
  float cellResistance[32] = {0};
  float avgCellVoltage = 0;
  float deltaCellVoltage = 0;
  uint8_t maxCellNum = 0;
  uint8_t minCellNum = 0;

  float powerTubeTemp = 0;
  float batteryVoltage = 0;
  float batteryPower = 0;      // Instant power (W)
  float chargeCurrent = 0;     // Positive=charging, Negative=discharging
  float tempSensor1 = 0;
  float tempSensor2 = 0;
  uint16_t errorsBitmask = 0;
  float balanceCurrent = 0;
  uint8_t balancingAction = 0; // 0=Off, 1=Charging balancer, 2=Discharging balancer
  uint8_t soc = 0;             // State of charge %
  float capacityRemain = 0;    // Remaining capacity (Ah)
  float nominalCapacity = 0;   // Total capacity (Ah)
  uint32_t cycleCount = 0;
  float cycleCapacity = 0;
  uint8_t soh = 0;             // State of health %
  uint32_t totalRuntime = 0;
  bool chargeMosfet = false;
  bool dischargeMosfet = false;
  bool balancing = false;

  // Device info (Frame 0x03)
  char vendorID[17] = {0};
  char hardwareVersion[9] = {0};
  char softwareVersion[9] = {0};
  char deviceName[17] = {0};
  uint32_t deviceUptime = 0;

  // Settings (Frame 0x01)
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
#define SCREEN_UPDATE_INTERVAL  500  // Update screen every 500ms

// ============== FUNCTION DECLARATIONS ==============
void setupDisplay();
void drawUI();
void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor);
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor);
void drawBluetoothIcon(int x, int y, bool connected);
void drawPowerCard();
void drawCapacityCard();
void drawTempCard();
void drawVoltageCurrentBar();
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
    // Check for frame start sequence: 0x55 0xAA 0xEB 0x90
    if (!frameStarted && i < length - 3 &&
        pData[i] == 0x55 && pData[i+1] == 0xAA &&
        pData[i+2] == 0xEB && pData[i+3] == 0x90) {
      frameStarted = true;
      rxIndex = 0;
      // Copy from start sequence
      for (size_t j = i; j < length && rxIndex < 320; j++) {
        rxBuffer[rxIndex++] = pData[j];
      }
      break;
    } else if (frameStarted && rxIndex < 320) {
      rxBuffer[rxIndex++] = pData[i];
    }
  }

  // Check if frame is complete (>= 300 bytes)
  if (frameStarted && rxIndex >= 300) {
    // Verify CRC
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
  Serial.println("JK-BMS Monitor Starting...");

  // Init display
  setupDisplay();

  // Init BLE
  BLEDevice::init("ESP32_JK_BMS_Monitor");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(30, false);  // Scan for 30 seconds

  Serial.println("Scanning for BMS...");
}

// ============== MAIN LOOP ==============
void loop() {
  // Handle BLE connection
  if (doConnect) {
    connectToBMS();
    doConnect = false;
  }

  // Reconnect if disconnected
  if (!connected && !doConnect && millis() - bmsData.lastUpdate > 10000) {
    Serial.println("Attempting reconnect...");
    BLEDevice::getScan()->start(10, false);
    bmsData.lastUpdate = millis();
  }

  // Update display
  if (millis() - lastScreenUpdate > SCREEN_UPDATE_INTERVAL) {
    drawUI();
    lastScreenUpdate = millis();
  }

  delay(10);
}

// ============== DISPLAY SETUP ==============
void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // Turn on backlight

  tft.init();
  tft.setRotation(1);  // Landscape mode
  tft.fillScreen(COLOR_BG);

  // Set text rendering mode
  tft.setTextDatum(MC_DATUM);

  screenInitialized = true;
}

// ============== DRAW ROUNDED RECTANGLE ==============
void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t fillColor, uint32_t borderColor) {
  // Fill
  tft.fillRoundRect(x, y, w, h, r, fillColor);
  // Border
  tft.drawRoundRect(x, y, w, h, r, borderColor);
}

// ============== DRAW PROGRESS BAR ==============
void drawProgressBar(int x, int y, int w, int h, int r, float percent, uint32_t barColor, uint32_t bgColor) {
  // Background
  tft.fillRoundRect(x, y, w, h, r, bgColor);
  // Progress fill
  int fillW = (int)((w - 4) * (percent / 100.0));
  if (fillW > 0) {
    if (fillW > w - 4) fillW = w - 4;
    tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, r - 2, barColor);
  }
}

// ============== DRAW BLUETOOTH ICON ==============
void drawBluetoothIcon(int x, int y, bool isConnected) {
  uint32_t color = isConnected ? COLOR_BLUETOOTH_ON : COLOR_BLUETOOTH_OFF;

  // Simple BT symbol using lines
  // Vertical line
  tft.drawLine(x + 4, y, x + 4, y + 14, color);
  // Diagonal lines
  tft.drawLine(x + 4, y, x + 9, y + 4, color);
  tft.drawLine(x + 9, y + 4, x, y + 10, color);
  tft.drawLine(x, y + 4, x + 9, y + 10, color);
  tft.drawLine(x + 9, y + 10, x + 4, y + 14, color);

  // Status dot
  if (isConnected) {
    tft.fillCircle(x + 12, y + 7, 2, COLOR_BLUETOOTH_ON);
  }
}

// ============== DRAW MAIN UI ==============
void drawUI() {
  if (!screenInitialized) return;

  // Clear only if needed (first draw or major change)
  static bool firstDraw = true;
  if (firstDraw) {
    tft.fillScreen(COLOR_BG);
    firstDraw = false;
  }

  // === HEADER ===
  // Title
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("JK-BMS Monitor", 12, 8, 2);

  // Bluetooth icon in top-right
  drawBluetoothIcon(SCREEN_WIDTH - 24, 8, bmsData.connected);

  // Connection status text
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(bmsData.connected ? COLOR_BLUETOOTH_ON : COLOR_BLUETOOTH_OFF, COLOR_BG);
  tft.drawString(bmsData.connected ? "ONLINE" : "OFFLINE", SCREEN_WIDTH - 30, 10, 1);

  // === MAIN POWER CARD (Large, centered-top) ===
  drawPowerCard();

  // === CAPACITY CARD (Bottom-left) ===
  drawCapacityCard();

  // === TEMPERATURE CARD (Bottom-right) ===
  drawTempCard();

  // === VOLTAGE/CURRENT BAR (Between power and bottom cards) ===
  drawVoltageCurrentBar();
}

// ============== DRAW POWER CARD ==============
void drawPowerCard() {
  int x = 12, y = 32, w = SCREEN_WIDTH - 24, h = 90, r = 12;

  // Card background with gradient effect (simulate with fill)
  drawRoundedRect(x, y, w, h, r, COLOR_POWER_BG, COLOR_CARD_BORDER);

  // Inner highlight
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, 0x004455);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_POWER_BG);
  tft.drawString("REAL-TIME POWER", x + 15, y + 10, 1);

  // Power value (large, centered in card)
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COLOR_POWER, COLOR_POWER_BG);

  float power = bmsData.batteryPower;
  char powerStr[16];
  if (power >= 1000) {
    snprintf(powerStr, sizeof(powerStr), "%.2f kW", power / 1000.0);
  } else {
    snprintf(powerStr, sizeof(powerStr), "%.0f W", power);
  }
  tft.drawString(powerStr, x + w / 2, y + h / 2 + 5, 4);

  // Charging/Discharging indicator
  tft.setTextDatum(TC_DATUM);
  if (bmsData.chargeCurrent > 0.1) {
    tft.setTextColor(COLOR_CAPACITY, COLOR_POWER_BG);
    tft.drawString("CHARGING", x + w / 2, y + h - 18, 1);
  } else if (bmsData.chargeCurrent < -0.1) {
    tft.setTextColor(COLOR_TEMP, COLOR_POWER_BG);
    tft.drawString("DISCHARGING", x + w / 2, y + h - 18, 1);
  } else {
    tft.setTextColor(COLOR_TEXT_SECOND, COLOR_POWER_BG);
    tft.drawString("IDLE", x + w / 2, y + h - 18, 1);
  }
}

// ============== DRAW CAPACITY CARD ==============
void drawCapacityCard() {
  int x = 12, y = 172, w = 148, h = 60, r = 10;

  drawRoundedRect(x, y, w, h, r, COLOR_CAPACITY_BG, COLOR_CARD_BORDER);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CAPACITY_BG);
  tft.drawString("CAPACITY", x + 10, y + 6, 1);

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
  drawProgressBar(x + 10, y + 44, w - 20, 8, 4, bmsData.soc, COLOR_CAPACITY, COLOR_PROGRESS_BG);
}

// ============== DRAW TEMPERATURE CARD ==============
void drawTempCard() {
  int x = 172, y = 172, w = 136, h = 60, r = 10;

  drawRoundedRect(x, y, w, h, r, COLOR_TEMP_BG, COLOR_CARD_BORDER);

  // Label
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_TEMP_BG);
  tft.drawString("TEMPERATURE", x + 10, y + 6, 1);

  // Temperature values
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_TEMP, COLOR_TEMP_BG);
  char tempStr[24];

  // Show the higher of T1/T2, or average
  float avgTemp = (bmsData.tempSensor1 + bmsData.tempSensor2) / 2.0;
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

  // Temp bar (visual indicator)
  float tempPercent = (avgTemp + 20) / 80.0 * 100;  // Map -20 to 60C
  if (tempPercent < 0) tempPercent = 0;
  if (tempPercent > 100) tempPercent = 100;
  drawProgressBar(x + 10, y + 44, w - 20, 8, 4, tempPercent, COLOR_TEMP, COLOR_PROGRESS_BG);
}

// ============== DRAW VOLTAGE/CURRENT BAR ==============
void drawVoltageCurrentBar() {
  int x = 12, y = 130, w = SCREEN_WIDTH - 24, h = 34, r = 8;

  drawRoundedRect(x, y, w, h, r, COLOR_CARD_BG, COLOR_CARD_BORDER);

  // Voltage (left side)
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COLOR_VOLTAGE, COLOR_CARD_BG);
  char voltStr[16];
  snprintf(voltStr, sizeof(voltStr), "%.2fV", bmsData.batteryVoltage);
  tft.drawString(voltStr, x + 12, y + 10, 2);

  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("VOLTAGE", x + 12, y + 2, 1);

  // Current (right side)
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(COLOR_CURRENT, COLOR_CARD_BG);
  char currStr[16];
  snprintf(currStr, sizeof(currStr), "%.2fA", bmsData.chargeCurrent);
  tft.drawString(currStr, x + w - 12, y + 10, 2);

  tft.setTextColor(COLOR_TEXT_SECOND, COLOR_CARD_BG);
  tft.drawString("CURRENT", x + w - 12, y + 2, 1);

  // Center divider
  tft.drawLine(x + w / 2, y + 6, x + w / 2, y + h - 6, COLOR_CARD_BORDER);

  // Small current direction indicator
  int cx = x + w / 2;
  int cy = y + h / 2;
  if (bmsData.chargeCurrent > 0.1) {
    // Charging arrow up
    tft.fillTriangle(cx - 4, cy + 3, cx + 4, cy + 3, cx, cy - 5, COLOR_CAPACITY);
  } else if (bmsData.chargeCurrent < -0.1) {
    // Discharging arrow down
    tft.fillTriangle(cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 5, COLOR_TEMP);
  } else {
    tft.fillCircle(cx, cy, 3, COLOR_TEXT_SECOND);
  }
}

// ============== PARSE JK02_32S FRAME ==============
void parseJK02_32S_Frame(uint8_t* data, uint16_t len) {
  if (len < 300) return;

  uint8_t frameType = data[4];

  switch (frameType) {
    case 0x02: {  // Cell info frame
      // Cell voltages (bytes 6-69, 32 x uint16 LE, 0.001V)
      for (int i = 0; i < 32; i++) {
        int offset = 6 + i * 2;
        bmsData.cellVoltage[i] = (data[offset] | (data[offset + 1] << 8)) * 0.001;
      }

      // Average cell voltage (bytes 74-75)
      bmsData.avgCellVoltage = (data[74] | (data[75] << 8)) * 0.001;

      // Delta cell voltage (bytes 76-77)
      bmsData.deltaCellVoltage = (data[76] | (data[77] << 8)) * 0.001;

      // Max/Min cell numbers (bytes 78-79)
      bmsData.maxCellNum = data[78];
      bmsData.minCellNum = data[79];

      // Cell resistances (bytes 80-143)
      for (int i = 0; i < 32; i++) {
        int offset = 80 + i * 2;
        bmsData.cellResistance[i] = (data[offset] | (data[offset + 1] << 8)) * 0.001;
      }

      // Power tube temperature (bytes 144-145, int16 LE, 0.1C)
      bmsData.powerTubeTemp = (int16_t)(data[144] | (data[145] << 8)) * 0.1;

      // Battery voltage (bytes 150-153, uint32 LE, 0.001V)
      bmsData.batteryVoltage = ((uint32_t)data[150] | ((uint32_t)data[151] << 8) |
                                ((uint32_t)data[152] << 16) | ((uint32_t)data[153] << 24)) * 0.001;

      // Battery power (bytes 154-157, uint32 LE, 0.001W)
      bmsData.batteryPower = ((uint32_t)data[154] | ((uint32_t)data[155] << 8) |
                              ((uint32_t)data[156] << 16) | ((uint32_t)data[157] << 24)) * 0.001;

      // Charge current (bytes 158-161, int32 LE, 0.001A)
      bmsData.chargeCurrent = (int32_t)((uint32_t)data[158] | ((uint32_t)data[159] << 8) |
                                        ((uint32_t)data[160] << 16) | ((uint32_t)data[161] << 24)) * 0.001;

      // Temperature sensors (bytes 162-165, int16 LE, 0.1C)
      bmsData.tempSensor1 = (int16_t)(data[162] | (data[163] << 8)) * 0.1;
      bmsData.tempSensor2 = (int16_t)(data[164] | (data[165] << 8)) * 0.1;

      // Errors bitmask (bytes 166-167)
      bmsData.errorsBitmask = data[166] | (data[167] << 8);

      // Balance current (bytes 170-171, int16 LE, 0.001A)
      bmsData.balanceCurrent = (int16_t)(data[170] | (data[171] << 8)) * 0.001;

      // Balancing action (byte 172)
      bmsData.balancingAction = data[172];

      // State of charge (byte 173)
      bmsData.soc = data[173];

      // Remaining capacity (bytes 174-177, uint32 LE, 0.001Ah)
      bmsData.capacityRemain = ((uint32_t)data[174] | ((uint32_t)data[175] << 8) |
                                ((uint32_t)data[176] << 16) | ((uint32_t)data[177] << 24)) * 0.001;

      // Nominal capacity (bytes 178-181, uint32 LE, 0.001Ah)
      bmsData.nominalCapacity = ((uint32_t)data[178] | ((uint32_t)data[179] << 8) |
                                 ((uint32_t)data[180] << 16) | ((uint32_t)data[181] << 24)) * 0.001;

      // Cycle count (bytes 182-185, uint32 LE)
      bmsData.cycleCount = ((uint32_t)data[182] | ((uint32_t)data[183] << 8) |
                            ((uint32_t)data[184] << 16) | ((uint32_t)data[185] << 24));

      // Total cycle capacity (bytes 186-189, uint32 LE, 0.001Ah)
      bmsData.cycleCapacity = ((uint32_t)data[186] | ((uint32_t)data[187] << 8) |
                               ((uint32_t)data[188] << 16) | ((uint32_t)data[189] << 24)) * 0.001;

      // SOH (byte 190)
      bmsData.soh = data[190];

      // Total runtime (bytes 194-197, uint32 LE, seconds)
      bmsData.totalRuntime = ((uint32_t)data[194] | ((uint32_t)data[195] << 8) |
                              ((uint32_t)data[196] << 16) | ((uint32_t)data[197] << 24));

      // MOSFET states (bytes 198-201)
      bmsData.chargeMosfet = data[198] != 0;
      bmsData.dischargeMosfet = data[199] != 0;
      bmsData.balancing = data[201] != 0;

      Serial.printf("[BMS] V:%.2fV I:%.2fA P:%.1fW SOC:%d%% T1:%.1fC T2:%.1fC MOS:%.1fC\n",
                    bmsData.batteryVoltage, bmsData.chargeCurrent, bmsData.batteryPower,
                    bmsData.soc, bmsData.tempSensor1, bmsData.tempSensor2, bmsData.powerTubeTemp);
      break;
    }

    case 0x03: {  // Device info frame
      // Vendor ID (bytes 6-21)
      memcpy(bmsData.vendorID, &data[6], 16);
      bmsData.vendorID[16] = '\0';

      // Hardware version (bytes 22-29)
      memcpy(bmsData.hardwareVersion, &data[22], 8);
      bmsData.hardwareVersion[8] = '\0';

      // Software version (bytes 30-37)
      memcpy(bmsData.softwareVersion, &data[30], 8);
      bmsData.softwareVersion[8] = '\0';

      // Device uptime (bytes 38-41)
      bmsData.deviceUptime = ((uint32_t)data[38] | ((uint32_t)data[39] << 8) |
                              ((uint32_t)data[40] << 16) | ((uint32_t)data[41] << 24));

      // Device name (bytes 46-61)
      memcpy(bmsData.deviceName, &data[46], 16);
      bmsData.deviceName[16] = '\0';

      Serial.printf("[BMS] Device: %s HW:%s SW:%s\n",
                    bmsData.deviceName, bmsData.hardwareVersion, bmsData.softwareVersion);
      break;
    }

    case 0x01: {  // Settings frame
      // Cell count (byte 114)
      bmsData.cellCount = data[114];

      // Total battery capacity (bytes 130-133, uint32 LE, 0.001Ah)
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
    0xAA, 0x55, 0x90, 0xEB,  // Header
    cmd,                       // Command
    0x00,                      // Length
    0x00, 0x00, 0x00, 0x00,   // Data padding
    0x00, 0x00, 0x00, 0x00,   // Data padding
    0x00, 0x00,               // Padding
    sequenceCounter++,         // Sequence counter
    0x00, 0x00,               // Reserved
    0x00                       // CRC (will be calculated)
  };

  // Calculate CRC (sum of bytes 0-18)
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

  // Get service
  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("Service not found!");
    pClient->disconnect();
    return;
  }

  // Get characteristic
  pRemoteChar = pRemoteService->getCharacteristic(CHAR_UUID);
  if (pRemoteChar == nullptr) {
    Serial.println("Characteristic not found!");
    pClient->disconnect();
    return;
  }

  // Subscribe to notifications
  if (pRemoteChar->canNotify()) {
    pRemoteChar->registerForNotify(notifyCallback);
    Serial.println("Notifications registered");
  }

  // Send initial commands to start data stream
  delay(500);
  sendCommand(CMD_DEVICE_INFO);  // 0x97
  delay(500);
  sendCommand(CMD_CELL_INFO);    // 0x96

  connected = true;
  bmsData.connected = true;
  Serial.println("BMS ready - waiting for data...");
}
