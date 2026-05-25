#ifndef BATTLE_UI_H
#define BATTLE_UI_H

#include <TFT_eSPI.h>
#include <JKBMS_Protocol.h"
#include <SPI.h>

#define BATTLE_COLOR_LOW 0x001F
#define BATTLE_COLOR_MED 0x07E0
#define BATTLE_COLOR_HIGH 0xF800
#define BATTLE_COLOR_MAX 0xFFE0

#define CARD_BG_DARK 0x1082
#define CARD_BG_LIGHT 0x2945

class BattleUI {
public:
    BattleUI(TFT_eSPI* tft);
    void begin();
    void update(JKBMSData* bmsData, bool bluetoothConnected);
    void drawBackground();
    void drawRoundedRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
    void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawPowerGauge(float power, float maxPower);
    void drawCapacity(float remaining, float total);
    void drawTemperatures(int16_t temp1, int16_t temp2);
    void drawBluetoothStatus(bool connected);
    void drawTitle();
    void drawCellVoltages(float* voltages, uint8_t count);
    void drawProtectionStatus(uint16_t flags);
    void drawVoltageCurrent(float voltage, float current);
    void drawCycleCount(uint16_t cycles);
    void drawSOC(uint8_t soc);
    void drawChargingStatus(bool charging, bool discharging);
    void drawDynamicEffects();
    void drawFooter();
    
private:
    TFT_eSPI* tft;
    JKBMSData* currentData;
    bool btConnected;
    uint16_t currentBaseColor;
    uint16_t prevBaseColor;
    int animationFrame;
    float peakPower;
    unsigned long lastUpdateTime;
    
    uint16_t getPowerColor(float power, float maxPower);
    uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio);
    void drawGaugeArc(int16_t cx, int16_t cy, int16_t r, float startAngle, float endAngle, uint16_t color);
    void drawArrow(int16_t x, int16_t y, int16_t size, uint16_t color);
};

#endif
