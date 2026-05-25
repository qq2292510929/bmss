#include "Battle_UI.h"
#include <Arduino.h>

BattleUI::BattleUI(TFT_eSPI* display) {
    tft = display;
    currentData = nullptr;
    btConnected = false;
    currentBaseColor = BATTLE_COLOR_LOW;
    animationFrame = 0;
}

void BattleUI::begin() {
    tft->setTextDatum(MC_DATUM);
    tft->setTextWrap(false);
    tft->fillScreen(TFT_BLACK);
    drawBackground();
}

void BattleUI::drawBackground() {
    tft->fillScreen(TFT_BLACK);
    
    for (int i = 0; i < 40; i += 2) {
        uint16_t bgColor = tft->color565(16 + i/2, 16 + i/2, 32 + i);
        tft->drawFastHLine(0, i, 240, bgColor);
        tft->drawFastHLine(0, 319 - i, 240, bgColor);
    }
    
    for (int x = 0; x < 240; x += 20) {
        uint16_t lineColor = tft->color565(30, 30, 50);
        tft->drawLine(x, 0, x, 320, lineColor);
    }
    for (int y = 0; y < 320; y += 20) {
        tft->drawLine(0, y, 240, y, lineColor);
    }
}

uint16_t BattleUI::interpolateColor(uint16_t color1, uint16_t color2, float ratio) {
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    return tft->color565(r, g, b);
}

uint16_t BattleUI::getPowerColor(float power, float maxPower) {
    float ratio = abs(power) / maxPower;
    
    if (ratio < 0.33f) {
        return interpolateColor(BATTLE_COLOR_LOW, BATTLE_COLOR_MED, ratio * 3.0f);
    } else if (ratio < 0.66f) {
        return interpolateColor(BATTLE_COLOR_MED, BATTLE_COLOR_HIGH, (ratio - 0.33f) * 3.0f);
    } else {
        return interpolateColor(BATTLE_COLOR_HIGH, BATTLE_COLOR_MAX, (ratio - 0.66f) * 3.0f);
    }
}

void BattleUI::drawTitle() {
    int16_t titleX = 120;
    int16_t titleY = 12;
    
    uint16_t titleColor;
    if (currentData != nullptr) {
        titleColor = getPowerColor(currentData->power, 5000.0f);
    } else {
        titleColor = CARD_BG_DARK;
    }
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(titleColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    tft->drawString("BATTLE MODE", titleX, titleY);
    
    drawBluetoothStatus(btConnected);
}

void BattleUI::drawBluetoothStatus(bool connected) {
    int16_t iconX = 210;
    int16_t iconY = 12;
    
    if (connected) {
        uint16_t pulseColor = interpolateColor(0x07E0, 0xFFFF, (sin(animationFrame * 0.1) + 1) / 2);
        tft->fillCircle(iconX, iconY, 8, pulseColor);
        tft->drawCircle(iconX, iconY, 8, 0xFFFF);
        
        tft->setTextColor(TFT_BLACK, pulseColor);
        tft->setFreeFont(&NotoSansBold8);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("B", iconX, iconY);
        
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(0x07E0, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold8);
        tft->drawString("ONLINE", iconX - 28, iconY + 12);
    } else {
        tft->fillCircle(iconX, iconY, 8, 0xF800);
        tft->drawCircle(iconX, iconY, 8, 0xFFFF);
        
        tft->setTextColor(TFT_BLACK, 0xF800);
        tft->setFreeFont(&NotoSansBold8);
        tft->setTextDatum(MC_DATUM);
        tft->drawString("X", iconX, iconY);
        
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(0xF800, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold8);
        tft->drawString("OFFLINE", iconX - 30, iconY + 12);
    }
}

void BattleUI::drawPowerGauge(float power, float maxPower) {
    int16_t centerX = 120;
    int16_t centerY = 95;
    int16_t radius = 70;
    
    uint16_t bgColor = CARD_BG_DARK;
    tft->fillRoundRect(centerX - 85, centerY - 65, 170, 130, 8, bgColor);
    
    uint16_t arcBgColor = tft->color565(40, 40, 60);
    tft->drawArc(centerX, centerY, radius - 15, radius - 5, 150, 240, arcBgColor);
    tft->drawArc(centerX, centerY, radius - 5, radius + 5, 150, 240, arcBgColor);
    
    float powerRatio = constrain(abs(power) / maxPower, 0.0f, 1.0f);
    uint16_t powerColor = getPowerColor(power, maxPower);
    tft->drawArc(centerX, centerY, radius - 15, radius - 5, 150, 150 + 90 * powerRatio, powerColor);
    
    if (abs(power) > 100) {
        tft->drawArc(centerX, centerY, radius - 5, radius + 5, 150, 150 + 90 * powerRatio, powerColor);
    }
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold20);
    tft->drawString(String((int)abs(power)).c_str(), centerX, centerY - 10);
    
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("W", centerX + 40, centerY + 5);
    
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("REAL POWER", centerX, centerY - 40);
    
    String powerLabel = "STANDBY";
    if (power > 0) {
        powerLabel = "DISCHARGING";
    } else if (power < 0) {
        powerLabel = "CHARGING";
    }
    
    tft->setTextColor(powerColor, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString(powerLabel.c_str(), centerX, centerY + 35);
}

void BattleUI::drawCapacity(float remaining, float total) {
    int16_t cardX = 15;
    int16_t cardY = 145;
    int16_t cardW = 210;
    int16_t cardH = 60;
    
    uint16_t cardColor = CARD_BG_LIGHT;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(0xC618, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold10);
    tft->drawString("CAPACITY", cardX + 10, cardY + 12);
    
    float ratio = total > 0 ? (remaining / total) : 0;
    uint16_t barBgColor = tft->color565(30, 30, 50);
    tft->fillRoundRect(cardX + 10, cardY + 28, cardW - 20, 20, 5, barBgColor);
    
    uint16_t barColor = interpolateColor(0x07E0, 0xF800, 1.0f - ratio);
    tft->fillRoundRect(cardX + 10, cardY + 28, (cardW - 20) * ratio, 20, 5, barColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    String capacityStr = String(remaining, 1) + " / " + String(total, 1) + " Ah";
    tft->drawString(capacityStr.c_str(), cardX + cardW / 2, cardY + 38);
    
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    String percentStr = String((int)(ratio * 100)) + "%";
    tft->drawString(percentStr.c_str(), cardX + cardW - 15, cardY + 12);
}

void BattleUI::drawTemperatures(int16_t temp1, int16_t temp2) {
    int16_t cardX = 15;
    int16_t cardY = 215;
    int16_t cardW = 100;
    int16_t cardH = 55;
    
    uint16_t cardColor = CARD_BG_DARK;
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("TEMPERATURE", cardX + cardW / 2, cardY + 10);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0xFFFF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold12);
    String temp1Str = String(temp1) + "C";
    tft->drawString(temp1Str.c_str(), cardX + 35, cardY + 32);
    
    tft->setFreeFont(&NotoSansBold10);
    tft->setTextColor(0xC618, TFT_BLACK);
    String temp2Str = String(temp2) + "C";
    tft->drawString(temp2Str.c_str(), cardX + 75, cardY + 32);
    
    int16_t card2X = 125;
    tft->fillRoundRect(card2X, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextColor(0x07FF, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    tft->drawString("VOLTAGE", card2X + cardW / 2, cardY + 10);
}

void BattleUI::drawSOC(uint8_t soc) {
    int16_t cardX = 15;
    int16_t cardY = 280;
    int16_t cardW = 210;
    int16_t cardH = 35;
    
    uint16_t cardColor = interpolateColor(0x07E0, 0xF800, 1.0f - soc / 100.0f);
    tft->fillRoundRect(cardX, cardY, cardW, cardH, 8, cardColor);
    
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_BLACK, cardColor);
    tft->setFreeFont(&NotoSansBold12);
    String socStr = "SOC: " + String(soc) + "%";
    tft->drawString(socStr.c_str(), cardX + cardW / 2, cardY + cardH / 2);
}

void BattleUI::drawFooter(JKBMSData* data) {
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(0x8410, TFT_BLACK);
    tft->setFreeFont(&NotoSansBold8);
    
    if (data != nullptr) {
        String footer = "Cycles: " + String(data->cycleCount) + " | Cells: " + String(data->cellCount) + "S";
        tft->drawString(footer.c_str(), 120, 310);
    } else {
        tft->drawString("Waiting for data...", 120, 310);
    }
}

void BattleUI::update(JKBMSData* bmsData, bool bluetoothConnected) {
    currentData = bmsData;
    btConnected = bluetoothConnected;
    
    animationFrame++;
    
    drawBackground();
    drawTitle();
    
    if (bmsData != nullptr) {
        drawPowerGauge(bmsData->power, 5000.0f);
        drawCapacity(bmsData->capacityRemaining, bmsData->capacityTotal);
        drawTemperatures(bmsData->temperature1, bmsData->temperature2);
        drawSOC((uint8_t)bmsData->soc);
    } else {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(0xFFFF, TFT_BLACK);
        tft->setFreeFont(&NotoSansBold16);
        tft->drawString("WAITING...", 120, 120);
        
        tft->setFreeFont(&NotoSansBold10);
        tft->drawString("Scanning JK_BD4A24S10P", 120, 150);
        
        float pulseSize = (sin(animationFrame * 0.1) + 1) * 3 + 5;
        uint16_t pulseColor = interpolateColor(0x07E0, 0xFFFF, (sin(animationFrame * 0.1) + 1) / 2);
        tft->drawCircle(120, 200, (int)pulseSize, pulseColor);
    }
    
    drawFooter(bmsData);
}
