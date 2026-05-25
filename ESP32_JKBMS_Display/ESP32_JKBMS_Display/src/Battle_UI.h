#ifndef BATTLE_UI_H
#define BATTLE_UI_H

#include <TFT_eSPI.h>
#include "JKBMS_Protocol.h"
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
    void drawPowerGauge(float power, float maxPower);
    void drawCapacity(float remaining, float total);
    void drawTemperatures(int16_t temp1, int16_t temp2);
    void drawBluetoothStatus(bool connected);
    void drawTitle();
    void drawSOC(uint8_t soc);
    void drawFooter(JKBMSData* data);
    
private:
    TFT_eSPI* tft;
    JKBMSData* currentData;
    bool btConnected;
    uint16_t currentBaseColor;
    int animationFrame;
    
    uint16_t getPowerColor(float power, float maxPower);
    uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio);
};

#endif
