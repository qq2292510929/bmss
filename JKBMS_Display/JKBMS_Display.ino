/*
 * ================================================================
 *  JKBMS BLE Monitor v3 - ESP32 + ST7789 + LVGL
 *  精仿参考设计版本
 * ================================================================
 *
 *  Hardware: ESP32-WROOM-32E + 2.8" ST7789P3 (320x240)
 *  BMS: JK-BD4A24S10P (JK02_32S protocol)
 *  BLE MAC: 98:da:20:07:b9:00
 *
 *  UI特色:
 *    - 左上角弧形角标随电流四色变化
 *    - 大号功率显示带单位
 *    - 容量显示带红色斜杠
 *    - 温度显示带图标
 *    - 几何线条背景装饰
 *
 * ================================================================
 */

#define USE_CN_FONT

#include "lv_conf.h"
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>

#ifdef USE_CN_FONT
LV_FONT_DECLARE(cn_font_16);
#define CN_FONT &cn_font_16
#else
#define CN_FONT &lv_font_montserrat_14
#endif

#define JKBMS_MAC  "98:da:20:07:b9:00"

#define CMD_CELL_INFO   0x96
#define CMD_DEVICE_INFO 0x97

#define FRAME_MIN_SIZE  300
#define FRAME_MAX_SIZE  320

#define SCREEN_W 320
#define SCREEN_H 240
#define BUF_LINES 20

#define RECONNECT_MS  5000
#define CMD_INTERVAL  3000
#define UI_INTERVAL   500

static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pWriteChar = nullptr;
static BLERemoteCharacteristic* pNotifyChar = nullptr;
static bool bleConnected = false;
static unsigned long lastCmdTime = 0;
static unsigned long lastReconnTime = 0;

static uint8_t frameBuf[FRAME_MAX_SIZE];
static uint16_t frameIdx = 0;
static bool frameStarted = false;
static uint8_t hdrState = 0;

struct BMSData {
    float voltage;
    float current;
    float power;
    float temp1;
    float temp2;
    uint8_t soc;
    float remainCap;
    float nominalCap;
    bool charging;
    bool discharging;
    bool balancing;
    uint16_t errors;
    bool online;
    unsigned long lastUpdate;
};

static BMSData bms;

static TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf[SCREEN_W * BUF_LINES];

static lv_obj_t* scr;
static lv_obj_t* cornerArc;
static lv_obj_t* lblPower;
static lv_obj_t* lblPowerUnit;
static lv_obj_t* lblCapUsed;
static lv_obj_t* lblCapSlash;
static lv_obj_t* lblCapTotal;
static lv_obj_t* lblCapUnit;
static lv_obj_t* barPower;
static lv_obj_t* barCap;
static lv_obj_t* lblTemp;
static lv_obj_t* lblBle;
static lv_obj_t* lblStatus;
static lv_obj_t* lineDecor1;
static lv_obj_t* lineDecor2;
static lv_obj_t* lineDecor3;

static lv_style_t styleCornerArc;
static lv_style_t stylePowerNum;
static lv_style_t stylePowerUnit;
static lv_style_t styleCapNum;
static lv_style_t styleCapSlash;
static lv_style_t styleCapUnit;
static lv_style_t styleBarBg;
static lv_style_t styleBarGreen;
static lv_style_t styleBarOrange;
static lv_style_t styleBarCyan;
static lv_style_t styleDetail;
static lv_style_t styleDecorLine;

static uint8_t calcCRC(const uint8_t* d, uint16_t len) {
    uint8_t c = 0;
    for (uint16_t i = 0; i < len; i++) c += d[i];
    return c;
}

static uint32_t readU32(const uint8_t* d, uint16_t o) {
    return (uint32_t)d[o] | ((uint32_t)d[o+1]<<8) | ((uint32_t)d[o+2]<<16) | ((uint32_t)d[o+3]<<24);
}

static int32_t readI32(const uint8_t* d, uint16_t o) {
    return (int32_t)readU32(d, o);
}

static uint16_t readU16(const uint8_t* d, uint16_t o) {
    return (uint16_t)d[o] | ((uint16_t)d[o+1]<<8);
}

static int16_t readI16(const uint8_t* d, uint16_t o) {
    return (int16_t)readU16(d, o);
}

static void sendCmd(uint8_t cmd) {
    if (!pWriteChar || !bleConnected) return;
    uint8_t f[20] = {0xAA, 0x55, 0x90, 0xEB, cmd, 0x00};
    f[19] = calcCRC(f, 19);
    pWriteChar->writeValue(f, 20, false);
}

static void parseCellInfo(const uint8_t* d, uint16_t len) {
    if (len < FRAME_MIN_SIZE) return;
    if (d[4] != 0x02) return;
    if (calcCRC(d, len - 1) != d[len - 1]) {
        Serial.println("CRC fail");
        return;
    }

    bms.voltage   = readU32(d, 150) / 1000.0f;
    bms.power     = readU32(d, 154) / 1000.0f;
    bms.current   = readI32(d, 158) / 1000.0f;
    bms.temp1     = readI16(d, 162) / 10.0f;
    bms.temp2     = readI16(d, 164) / 10.0f;
    bms.errors    = readU16(d, 166);
    bms.balancing = (d[172] != 0);
    bms.soc       = d[173];
    bms.remainCap = readU32(d, 174) / 1000.0f;
    bms.nominalCap= readU32(d, 178) / 1000.0f;
    bms.charging  = (d[198] == 1);
    bms.discharging = (d[199] == 1);
    bms.lastUpdate = millis();
    bms.online    = true;

    Serial.printf("V=%.2f I=%.2f P=%.1f SOC=%d T1=%.1f Rem=%.1f/%.1f C=%d D=%d\n",
        bms.voltage, bms.current, bms.power, bms.soc,
        bms.temp1, bms.remainCap, bms.nominalCap, bms.charging, bms.discharging);
}

static void notifyCB(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
    for (size_t i = 0; i < length; i++) {
        uint8_t b = pData[i];

        if (!frameStarted) {
            switch (hdrState) {
                case 0: hdrState = (b == 0x55) ? 1 : 0; break;
                case 1: hdrState = (b == 0xAA) ? 2 : (b == 0x55 ? 1 : 0); break;
                case 2: hdrState = (b == 0xEB) ? 3 : (b == 0x55 ? 1 : 0); break;
                case 3:
                    if (b == 0x90) {
                        frameBuf[0] = 0x55;
                        frameBuf[1] = 0xAA;
                        frameBuf[2] = 0xEB;
                        frameBuf[3] = 0x90;
                        frameIdx = 4;
                        frameStarted = true;
                        hdrState = 0;
                    } else {
                        hdrState = (b == 0x55) ? 1 : 0;
                    }
                    break;
            }
            continue;
        }

        frameBuf[frameIdx++] = b;

        if (frameIdx >= FRAME_MIN_SIZE) {
            if (calcCRC(frameBuf, frameIdx - 1) == frameBuf[frameIdx - 1]) {
                parseCellInfo(frameBuf, frameIdx);
            }
            frameIdx = 0;
            frameStarted = false;
        } else if (frameIdx >= FRAME_MAX_SIZE) {
            frameIdx = 0;
            frameStarted = false;
        }
    }
}

class ClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { bleConnected = true; }
    void onDisconnect(BLEClient*) override { bleConnected = false; pWriteChar = nullptr; pNotifyChar = nullptr; }
};

static bool connectBMS() {
    if (pClient && pClient->isConnected()) { pClient->disconnect(); delay(500); }
    if (!pClient) { BLEDevice::init(""); pClient = BLEDevice::createClient(); pClient->setClientCallbacks(new ClientCB()); }

    BLEAddress addr(JKBMS_MAC);
    if (!pClient->connect(addr)) return false;

    BLERemoteService* pSvc = pClient->getService(BLEUUID((uint16_t)0xFFE0));
    if (!pSvc) { pClient->disconnect(); return false; }

    std::map<std::string, BLERemoteCharacteristic*>* charMap = pSvc->getCharacteristics();
    pWriteChar = nullptr; pNotifyChar = nullptr;

    for (auto& kv : *charMap) {
        BLERemoteCharacteristic* c = kv.second;
        if (c->canWrite() || c->canWriteNoResponse()) if (!pWriteChar) pWriteChar = c;
        if (c->canNotify() || c->canIndicate()) if (!pNotifyChar) pNotifyChar = c;
    }

    if (!pNotifyChar) for (auto& kv : *charMap) if (kv.second != pWriteChar) { pNotifyChar = kv.second; break; }
    if (!pWriteChar || !pNotifyChar) { pClient->disconnect(); return false; }

    pNotifyChar->registerForNotify(notifyCB, true);
    delay(300); sendCmd(CMD_DEVICE_INFO); delay(300); sendCmd(CMD_CELL_INFO); lastCmdTime = millis();
    return true;
}

static void myDispFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    tft.startWrite(); tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, true); tft.endWrite(); lv_disp_flush_ready(drv);
}

static void initDisplay() {
    tft.begin(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
    lv_init(); lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, SCREEN_W * BUF_LINES);
    static lv_disp_drv_t dispDrv; lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W; dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = myDispFlush; dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);
}

static void initStyles() {
    lv_style_init(&styleCornerArc);
    lv_style_set_bg_color(&styleCornerArc, lv_color_hex(0x39FF14));
    lv_style_set_bg_opa(&styleCornerArc, LV_OPA_COVER);
    lv_style_set_radius(&styleCornerArc, 20);
    lv_style_set_border_width(&styleCornerArc, 0);

    lv_style_init(&stylePowerNum);
    lv_style_set_text_font(&stylePowerNum, &lv_font_montserrat_48);
    lv_style_set_text_color(&stylePowerNum, lv_color_hex(0xFFFFFF));
    lv_style_set_text_align(&stylePowerNum, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&stylePowerUnit);
    lv_style_set_text_font(&stylePowerUnit, &lv_font_montserrat_20);
    lv_style_set_text_color(&stylePowerUnit, lv_color_hex(0x8888AA));
    lv_style_set_text_align(&stylePowerUnit, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&styleCapNum);
    lv_style_set_text_font(&styleCapNum, &lv_font_montserrat_36);
    lv_style_set_text_color(&styleCapNum, lv_color_hex(0xFFFFFF));
    lv_style_set_text_align(&styleCapNum, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&styleCapSlash);
    lv_style_set_text_font(&styleCapSlash, &lv_font_montserrat_44);
    lv_style_set_text_color(&styleCapSlash, lv_color_hex(0xFF4444));
    lv_style_set_text_align(&styleCapSlash, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&styleCapUnit);
    lv_style_set_text_font(&styleCapUnit, &lv_font_montserrat_18);
    lv_style_set_text_color(&styleCapUnit, lv_color_hex(0x666688));
    lv_style_set_text_align(&styleCapUnit, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&styleBarBg);
    lv_style_set_bg_color(&styleBarBg, lv_color_hex(0x1A1A2E));
    lv_style_set_bg_opa(&styleBarBg, LV_OPA_COVER);
    lv_style_set_radius(&styleBarBg, 3);

    lv_style_init(&styleBarGreen);
    lv_style_set_bg_color(&styleBarGreen, lv_color_hex(0x39FF14));
    lv_style_set_bg_opa(&styleBarGreen, LV_OPA_COVER);
    lv_style_set_radius(&styleBarGreen, 3);

    lv_style_init(&styleBarOrange);
    lv_style_set_bg_color(&styleBarOrange, lv_color_hex(0xFF6600));
    lv_style_set_bg_opa(&styleBarOrange, LV_OPA_COVER);
    lv_style_set_radius(&styleBarOrange, 3);

    lv_style_init(&styleBarCyan);
    lv_style_set_bg_color(&styleBarCyan, lv_color_hex(0x00D4FF));
    lv_style_set_bg_opa(&styleBarCyan, LV_OPA_COVER);
    lv_style_set_radius(&styleBarCyan, 3);

    lv_style_init(&styleDetail);
    lv_style_set_text_font(&styleDetail, &lv_font_montserrat_14);
    lv_style_set_text_color(&styleDetail, lv_color_hex(0x8888AA));
    lv_style_set_text_align(&styleDetail, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&styleDecorLine);
    lv_style_set_bg_color(&styleDecorLine, lv_color_hex(0x1A1A3E));
    lv_style_set_bg_opa(&styleDecorLine, LV_OPA_COVER);
    lv_style_set_radius(&styleDecorLine, 1);
}

static void createUI() {
    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    cornerArc = lv_obj_create(scr);
    lv_obj_set_size(cornerArc, 60, 60);
    lv_obj_align(cornerArc, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_style(cornerArc, &styleCornerArc, 0);

    lineDecor1 = lv_obj_create(scr);
    lv_obj_set_size(lineDecor1, 100, 1);
    lv_obj_align(lineDecor1, LV_ALIGN_TOP_LEFT, 90, 35);
    lv_obj_add_style(lineDecor1, &styleDecorLine, 0);

    lineDecor2 = lv_obj_create(scr);
    lv_obj_set_size(lineDecor2, 1, 80);
    lv_obj_align(lineDecor2, LV_ALIGN_TOP_RIGHT, -100, 15);
    lv_obj_add_style(lineDecor2, &styleDecorLine, 0);

    lineDecor3 = lv_obj_create(scr);
    lv_obj_set_size(lineDecor3, 80, 1);
    lv_obj_align(lineDecor3, LV_ALIGN_TOP_RIGHT, -20, 85);
    lv_obj_add_style(lineDecor3, &styleDecorLine, 0);

    lblBle = lv_label_create(scr);
    lv_obj_set_style_text_font(lblBle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblBle, lv_color_hex(0x666688), 0);
    lv_obj_align(lblBle, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_label_set_text(lblBle, "\xE2\x84\xA2");

    lblPower = lv_label_create(scr);
    lv_obj_add_style(lblPower, &stylePowerNum, 0);
    lv_obj_align(lblPower, LV_ALIGN_TOP_LEFT, 22, 50);
    lv_label_set_text(lblPower, "0.0");

    lblPowerUnit = lv_label_create(scr);
    lv_obj_add_style(lblPowerUnit, &stylePowerUnit, 0);
    lv_obj_align_to(lblPowerUnit, lblPower, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
    lv_label_set_text(lblPowerUnit, "w");

    barPower = lv_bar_create(scr);
    lv_obj_set_size(barPower, 270, 3);
    lv_obj_align(barPower, LV_ALIGN_TOP_LEFT, 25, 105);
    lv_bar_set_range(barPower, 0, 5000);
    lv_bar_set_value(barPower, 0, LV_ANIM_OFF);
    lv_obj_add_style(barPower, &styleBarBg, LV_PART_MAIN);
    lv_obj_add_style(barPower, &styleBarCyan, LV_PART_INDICATOR);

    lblCapUsed = lv_label_create(scr);
    lv_obj_add_style(lblCapUsed, &styleCapNum, 0);
    lv_obj_align(lblCapUsed, LV_ALIGN_TOP_LEFT, 22, 120);
    lv_label_set_text(lblCapUsed, "0");

    lblCapSlash = lv_label_create(scr);
    lv_obj_add_style(lblCapSlash, &styleCapSlash, 0);
    lv_obj_align_to(lblCapSlash, lblCapUsed, LV_ALIGN_OUT_RIGHT_TOP, 10, -2);
    lv_label_set_text(lblCapSlash, "/");

    lblCapTotal = lv_label_create(scr);
    lv_obj_add_style(lblCapTotal, &styleCapNum, 0);
    lv_obj_align_to(lblCapTotal, lblCapSlash, LV_ALIGN_OUT_RIGHT_TOP, 10, -2);
    lv_label_set_text(lblCapTotal, "0");

    lblCapUnit = lv_label_create(scr);
    lv_obj_add_style(lblCapUnit, &styleCapUnit, 0);
    lv_obj_align_to(lblCapUnit, lblCapTotal, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    lv_label_set_text(lblCapUnit, "Ah");

    barCap = lv_bar_create(scr);
    lv_obj_set_size(barCap, 270, 4);
    lv_obj_align(barCap, LV_ALIGN_TOP_LEFT, 25, 170);
    lv_bar_set_range(barCap, 0, 100);
    lv_bar_set_value(barCap, 0, LV_ANIM_OFF);
    lv_obj_add_style(barCap, &styleBarBg, LV_PART_MAIN);
    lv_obj_add_style(barCap, &styleBarGreen, LV_PART_INDICATOR);

    lblTemp = lv_label_create(scr);
    lv_obj_add_style(lblTemp, &styleDetail, 0);
    lv_obj_align(lblTemp, LV_ALIGN_BOTTOM_LEFT, 25, -12);
    lv_label_set_text(lblTemp, "\xE2\x9A\xA1 0.0\xC2\xB0""C");

    lblStatus = lv_label_create(scr);
    lv_obj_add_style(lblStatus, &styleDetail, 0);
    lv_obj_align(lblStatus, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_label_set_text(lblStatus, "BMS");
}

static void setCornerColor(lv_color_t color) {
    lv_obj_set_style_bg_color(cornerArc, color, 0);
}

static lv_color_t getCurrentColor(float current) {
    if (current > 0.5f)       return lv_color_hex(0x39FF14);
    else if (current > 0.05f) return lv_color_hex(0x00D4FF);
    else if (current < -0.5f) return lv_color_hex(0xFF6600);
    else if (current < -0.05f) return lv_color_hex(0xFFAA00);
    else                      return lv_color_hex(0x666688);
}

static void updateUI() {
    char buf[32];
    unsigned long age = millis() - bms.lastUpdate;
    bool dataFresh = bms.online && age < 10000;

    lv_label_set_text(lblBle, bleConnected ? "\xE2\x84\xA2" : "\xE2\x9D\x8C");
    lv_obj_set_style_text_color(lblBle, bleConnected ? lv_color_hex(0x39FF14) : lv_color_hex(0x666688), 0);

    if (!dataFresh && !bleConnected) {
        lv_label_set_text(lblPower, "--");
        lv_label_set_text(lblCapUsed, "--");
        lv_label_set_text(lblCapTotal, "--");
        lv_label_set_text(lblTemp, "\xE2\x9A\xA1 --\xC2\xB0""C");
        lv_label_set_text(lblStatus, "OFFLINE");
        setCornerColor(lv_color_hex(0x333355));
        return;
    }

    float absPower = fabs(bms.power);
    snprintf(buf, sizeof(buf), "%.1f", absPower);
    lv_label_set_text(lblPower, buf);

    lv_bar_set_value(barPower, (int32_t)absPower, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%.0f", bms.remainCap);
    lv_label_set_text(lblCapUsed, buf);
    snprintf(buf, sizeof(buf), "%.0f", bms.nominalCap);
    lv_label_set_text(lblCapTotal, buf);

    uint8_t soc = (bms.nominalCap > 0) ? (uint8_t)(bms.remainCap / bms.nominalCap * 100) : 0;
    lv_bar_set_value(barCap, soc, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "\xE2\x9A\xA1 %.1f\xC2\xB0""C", bms.temp1);
    lv_label_set_text(lblTemp, buf);

    lv_color_t color = getCurrentColor(bms.current);
    setCornerColor(color);

    if (bms.charging) {
        lv_label_set_text(lblStatus, "CHARGE");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x39FF14), 0);
    } else if (bms.discharging) {
        lv_label_set_text(lblStatus, "DISCHG");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0xFF6600), 0);
    } else {
        lv_label_set_text(lblStatus, "IDLE");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x666688), 0);
    }

    if (bms.current > 0.05f) {
        lv_obj_remove_style(barPower, &styleBarOrange, LV_PART_INDICATOR);
        lv_obj_remove_style(barPower, &styleBarCyan, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarGreen, LV_PART_INDICATOR);
    } else if (bms.current < -0.05f) {
        lv_obj_remove_style(barPower, &styleBarGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barPower, &styleBarCyan, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarOrange, LV_PART_INDICATOR);
    } else {
        lv_obj_remove_style(barPower, &styleBarGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barPower, &styleBarOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarCyan, LV_PART_INDICATOR);
    }

    if (soc > 60) {
        lv_obj_remove_style(barCap, &styleBarOrange, LV_PART_INDICATOR);
        lv_obj_remove_style(barCap, &styleBarCyan, LV_PART_INDICATOR);
        lv_obj_add_style(barCap, &styleBarGreen, LV_PART_INDICATOR);
    } else if (soc > 20) {
        lv_obj_remove_style(barCap, &styleBarGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barCap, &styleBarOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barCap, &styleBarCyan, LV_PART_INDICATOR);
    } else {
        lv_obj_remove_style(barCap, &styleBarGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barCap, &styleBarCyan, LV_PART_INDICATOR);
        lv_obj_add_style(barCap, &styleBarOrange, LV_PART_INDICATOR);
    }
}

void setup() {
    Serial.begin(115200); Serial.println("\nJKBMS Monitor v3 Starting...");
    memset(&bms, 0, sizeof(bms)); bms.temp1 = -999;
    initDisplay(); initStyles(); createUI();
    lastReconnTime = millis();
}

void loop() {
    lv_timer_handler();
    if (!bleConnected && millis() - lastReconnTime > RECONNECT_MS) { lastReconnTime = millis(); connectBMS(); }
    if (bleConnected && millis() - lastCmdTime > CMD_INTERVAL) { lastCmdTime = millis(); sendCmd(CMD_CELL_INFO); }
    static unsigned long lastUI = 0; if (millis() - lastUI > UI_INTERVAL) { lastUI = millis(); updateUI(); }
    delay(5);
}
