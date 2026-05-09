/*
 * ================================================================
 *  JKBMS BLE Monitor - ESP32 + ST7789 + LVGL
 * ================================================================
 *
 *  Hardware:
 *    - ESP32-WROOM-32E
 *    - 2.8" ST7789P3 Display (240x320, Landscape 320x240)
 *    - JKBMS JK-BD4A24S10P (JK02_32S protocol)
 *    - BLE MAC: 98:da:20:07:b9:00
 *
 *  Libraries (install via Arduino Library Manager):
 *    - TFT_eSPI by Bodmer (v2.4.79 for ESP32 Core 2.x)
 *    - LVGL v8.3.x by kisvegabor
 *    - ESP32 BLE Arduino (built-in with ESP32 core)
 *
 *  Display Wiring (ST7789 -> ESP32-32E):
 *    SCK  -> IO14 (TFT_SCK)
 *    SDA  -> IO13 (TFT_MOSI)
 *    CS   -> IO15 (TFT_CS)
 *    DC   -> IO2  (TFT_RS)
 *    RST  -> EN   (共享复位引脚)
 *    BL   -> IO21 (TFT_BL)
 *    GND  -> GND
 *    VCC  -> 3.3V
 *
 *  TFT_eSPI User_Setup.h settings:
 *    #define ST7789_DRIVER
 *    #define TFT_WIDTH  240
 *    #define TFT_HEIGHT 320
 *    #define TFT_MOSI 13
 *    #define TFT_SCLK 14
 *    #define TFT_CS   15
 *    #define TFT_DC   2
 *    #define TFT_RST  -1
 *    #define TFT_BL   21
 *    #define LOAD_GLCD
 *    #define LOAD_FONT2
 *    #define LOAD_GFXFF
 *    #define SMOOTH_FONT
 *    #define SPI_FREQUENCY  40000000
 *
 *  Chinese Font Generation:
 *    1. Visit https://lvgl.io/tools/fontconverter
 *    2. Settings: Name=cn_font_16, Size=16, Bpp=4, TTF=NotoSansSC-Regular
 *    3. Symbols: 功充放电压电流温度电池容量已用总瓦伏安度连接中状态健康运行秒时循环剩余额定待机实▲▼◆
 *    4. Download .c file, place in sketch folder
 *    5. Delete the line ".static_bitmap = 0," from the generated file (LVGL 9.x field)
 *    6. Uncomment #define USE_CN_FONT below
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
#define JKBMS_NAME "JK_BD4A24S10P"

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
static lv_obj_t* lblPowerTitle;
static lv_obj_t* lblPower;
static lv_obj_t* lblPowerUnit;
static lv_obj_t* lblMode;
static lv_obj_t* barPower;
static lv_obj_t* lblCapTitle;
static lv_obj_t* lblSoc;
static lv_obj_t* lblSocUnit;
static lv_obj_t* barSoc;
static lv_obj_t* lblCapDetail;
static lv_obj_t* lblTemp;
static lv_obj_t* lblVolt;
static lv_obj_t* lblCurr;
static lv_obj_t* lblStatus;
static lv_obj_t* contPower;
static lv_obj_t* contCap;
static lv_obj_t* contBottom;

static lv_style_t styleCont;
static lv_style_t stylePowerNum;
static lv_style_t styleSocNum;
static lv_style_t styleLabelSm;
static lv_style_t styleDetail;
static lv_style_t styleBarBg;
static lv_style_t styleBarIndGreen;
static lv_style_t styleBarIndOrange;
static lv_style_t styleBarIndCyan;

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

    bms.voltage   = readU32(d, 118) / 1000.0f;
    bms.power     = readU32(d, 122) / 1000.0f;
    bms.current   = readI32(d, 126) / 1000.0f;
    bms.temp1     = readI16(d, 130) / 10.0f;
    bms.temp2     = readI16(d, 132) / 10.0f;
    bms.errors    = readU16(d, 134);
    bms.balancing = (d[140] != 0);
    bms.soc       = d[141];
    bms.remainCap = readU32(d, 142) / 1000.0f;
    bms.nominalCap= readU32(d, 146) / 1000.0f;
    bms.charging  = (d[166] == 1);
    bms.discharging = (d[167] == 1);
    bms.lastUpdate = millis();
    bms.online    = true;

    Serial.printf("V=%.2f I=%.2f P=%.1f SOC=%d T1=%.1f Rem=%.1f/%.1f\n",
        bms.voltage, bms.current, bms.power, bms.soc,
        bms.temp1, bms.remainCap, bms.nominalCap);
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
    void onConnect(BLEClient*) override {
        Serial.println("BLE Connected");
        bleConnected = true;
    }
    void onDisconnect(BLEClient*) override {
        Serial.println("BLE Disconnected");
        bleConnected = false;
        pWriteChar = nullptr;
        pNotifyChar = nullptr;
    }
};

static bool connectBMS() {
    if (pClient && pClient->isConnected()) {
        pClient->disconnect();
        delay(500);
    }

    if (!pClient) {
        BLEDevice::init("");
        pClient = BLEDevice::createClient();
        pClient->setClientCallbacks(new ClientCB());
    }

    BLEAddress addr(JKBMS_MAC);
    Serial.printf("Connecting to %s ...\n", JKBMS_MAC);

    if (!pClient->connect(addr)) {
        Serial.println("Connect failed");
        return false;
    }

    Serial.println("Connected, discovering services...");

    BLERemoteService* pSvc = pClient->getService(BLEUUID((uint16_t)0xFFE0));
    if (!pSvc) {
        Serial.println("Service 0xFFE0 not found");
        pClient->disconnect();
        return false;
    }

    std::map<std::string, BLERemoteCharacteristic*>* charMap = pSvc->getCharacteristics();
    pWriteChar = nullptr;
    pNotifyChar = nullptr;

    for (auto& kv : *charMap) {
        BLERemoteCharacteristic* c = kv.second;
        Serial.printf("  Char UUID=%s handle=0x%04X\n",
            c->getUUID().toString().c_str(), c->getHandle());

        if (c->canWrite() || c->canWriteNoResponse()) {
            if (!pWriteChar) pWriteChar = c;
        }
        if (c->canNotify() || c->canIndicate()) {
            if (!pNotifyChar) pNotifyChar = c;
        }
    }

    if (!pNotifyChar) {
        for (auto& kv : *charMap) {
            if (kv.second != pWriteChar) {
                pNotifyChar = kv.second;
                break;
            }
        }
    }

    if (!pWriteChar || !pNotifyChar) {
        Serial.println("Characteristics not found");
        pClient->disconnect();
        return false;
    }

    Serial.printf("WriteChar handle=0x%04X NotifyChar handle=0x%04X\n",
        pWriteChar->getHandle(), pNotifyChar->getHandle());

    Serial.println("Registering notifications...");
    pNotifyChar->registerForNotify(notifyCB, true);

    delay(300);
    sendCmd(CMD_DEVICE_INFO);
    delay(300);
    sendCmd(CMD_CELL_INFO);
    lastCmdTime = millis();

    return true;
}

static void myDispFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void initDisplay() {
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, lvBuf, NULL, SCREEN_W * BUF_LINES);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W;
    dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = myDispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);
}

static void initStyles() {
    lv_style_init(&styleCont);
    lv_style_set_bg_color(&styleCont, lv_color_hex(0x0A0A0A));
    lv_style_set_bg_opa(&styleCont, LV_OPA_COVER);
    lv_style_set_border_color(&styleCont, lv_color_hex(0x1A1A2E));
    lv_style_set_border_width(&styleCont, 1);
    lv_style_set_radius(&styleCont, 6);
    lv_style_set_pad_all(&styleCont, 4);

    lv_style_init(&stylePowerNum);
    lv_style_set_text_font(&stylePowerNum, &lv_font_montserrat_48);
    lv_style_set_text_color(&stylePowerNum, lv_color_hex(0x39FF14));
    lv_style_set_text_align(&stylePowerNum, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&styleSocNum);
    lv_style_set_text_font(&styleSocNum, &lv_font_montserrat_40);
    lv_style_set_text_color(&styleSocNum, lv_color_hex(0x00D4FF));
    lv_style_set_text_align(&styleSocNum, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&styleLabelSm);
    lv_style_set_text_font(&styleLabelSm, CN_FONT);
    lv_style_set_text_color(&styleLabelSm, lv_color_hex(0x666688));
    lv_style_set_text_align(&styleLabelSm, LV_TEXT_ALIGN_LEFT);

    lv_style_init(&styleDetail);
    lv_style_set_text_font(&styleDetail, &lv_font_montserrat_18);
    lv_style_set_text_color(&styleDetail, lv_color_hex(0xAAAACC));
    lv_style_set_text_align(&styleDetail, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&styleBarBg);
    lv_style_set_bg_color(&styleBarBg, lv_color_hex(0x1A1A2E));
    lv_style_set_bg_opa(&styleBarBg, LV_OPA_COVER);
    lv_style_set_radius(&styleBarBg, 4);

    lv_style_init(&styleBarIndGreen);
    lv_style_set_bg_color(&styleBarIndGreen, lv_color_hex(0x39FF14));
    lv_style_set_bg_opa(&styleBarIndGreen, LV_OPA_COVER);
    lv_style_set_radius(&styleBarIndGreen, 4);

    lv_style_init(&styleBarIndOrange);
    lv_style_set_bg_color(&styleBarIndOrange, lv_color_hex(0xFF6600));
    lv_style_set_bg_opa(&styleBarIndOrange, LV_OPA_COVER);
    lv_style_set_radius(&styleBarIndOrange, 4);

    lv_style_init(&styleBarIndCyan);
    lv_style_set_bg_color(&styleBarIndCyan, lv_color_hex(0x00D4FF));
    lv_style_set_bg_opa(&styleBarIndCyan, LV_OPA_COVER);
    lv_style_set_radius(&styleBarIndCyan, 4);
}

static void createUI() {
    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lblStatus = lv_label_create(scr);
    lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x444466), 0);
    lv_obj_align(lblStatus, LV_ALIGN_TOP_RIGHT, -5, 2);
    lv_label_set_text(lblStatus, "BLE...");

    contPower = lv_obj_create(scr);
    lv_obj_set_size(contPower, 310, 105);
    lv_obj_align(contPower, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_add_style(contPower, &styleCont, 0);
    lv_obj_clear_flag(contPower, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(contPower, lv_color_hex(0x39FF14), 0);

    lblPowerTitle = lv_label_create(contPower);
    lv_obj_add_style(lblPowerTitle, &styleLabelSm, 0);
    lv_obj_align(lblPowerTitle, LV_ALIGN_TOP_MID, 0, 2);
#ifdef USE_CN_FONT
    lv_label_set_text(lblPowerTitle, "\xe5\xae\x9e\xe6\x97\xb6\xe5\x8a\x9f\xe7\x8e\x87");
#else
    lv_label_set_text(lblPowerTitle, "POWER");
#endif

    lblPower = lv_label_create(contPower);
    lv_obj_add_style(lblPower, &stylePowerNum, 0);
    lv_obj_align(lblPower, LV_ALIGN_CENTER, -25, 4);
    lv_label_set_text(lblPower, "0.0");

    lblPowerUnit = lv_label_create(contPower);
    lv_obj_set_style_text_font(lblPowerUnit, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lblPowerUnit, lv_color_hex(0x555577), 0);
    lv_obj_align_to(lblPowerUnit, lblPower, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, 0);
    lv_label_set_text(lblPowerUnit, "W");

    lblMode = lv_label_create(contPower);
    lv_obj_set_style_text_font(lblMode, CN_FONT, 0);
    lv_obj_set_style_text_color(lblMode, lv_color_hex(0x39FF14), 0);
    lv_obj_align(lblMode, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(lblMode, "--");

    barPower = lv_bar_create(scr);
    lv_obj_set_size(barPower, 310, 6);
    lv_obj_align_to(barPower, contPower, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
    lv_bar_set_range(barPower, 0, 5000);
    lv_bar_set_value(barPower, 0, LV_ANIM_OFF);
    lv_obj_add_style(barPower, &styleBarBg, LV_PART_MAIN);
    lv_obj_add_style(barPower, &styleBarIndGreen, LV_PART_INDICATOR);

    contCap = lv_obj_create(scr);
    lv_obj_set_size(contCap, 310, 80);
    lv_obj_align_to(contCap, barPower, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_add_style(contCap, &styleCont, 0);
    lv_obj_clear_flag(contCap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(contCap, lv_color_hex(0x00D4FF), 0);

    lblCapTitle = lv_label_create(contCap);
    lv_obj_add_style(lblCapTitle, &styleLabelSm, 0);
    lv_obj_align(lblCapTitle, LV_ALIGN_TOP_LEFT, 4, 2);
#ifdef USE_CN_FONT
    lv_label_set_text(lblCapTitle, "\xe7\x94\xb5\xe6\xb1\xa0\xe5\xae\xb9\xe9\x87\x8f");
#else
    lv_label_set_text(lblCapTitle, "CAPACITY");
#endif

    lblSoc = lv_label_create(contCap);
    lv_obj_add_style(lblSoc, &styleSocNum, 0);
    lv_obj_align(lblSoc, LV_ALIGN_LEFT_MID, 8, 4);
    lv_label_set_text(lblSoc, "0");

    lblSocUnit = lv_label_create(contCap);
    lv_obj_set_style_text_font(lblSocUnit, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lblSocUnit, lv_color_hex(0x555577), 0);
    lv_obj_align_to(lblSocUnit, lblSoc, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 0);
    lv_label_set_text(lblSocUnit, "%");

    barSoc = lv_bar_create(contCap);
    lv_obj_set_size(barSoc, 140, 14);
    lv_obj_align(barSoc, LV_ALIGN_RIGHT_MID, -8, -4);
    lv_bar_set_range(barSoc, 0, 100);
    lv_bar_set_value(barSoc, 0, LV_ANIM_OFF);
    lv_obj_add_style(barSoc, &styleBarBg, LV_PART_MAIN);
    lv_obj_add_style(barSoc, &styleBarIndCyan, LV_PART_INDICATOR);

    lblCapDetail = lv_label_create(contCap);
    lv_obj_set_style_text_font(lblCapDetail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblCapDetail, lv_color_hex(0x666688), 0);
    lv_obj_align(lblCapDetail, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(lblCapDetail, "0.0 / 0.0 Ah");

    contBottom = lv_obj_create(scr);
    lv_obj_set_size(contBottom, 310, 32);
    lv_obj_align_to(contBottom, contCap, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_add_style(contBottom, &styleCont, 0);
    lv_obj_clear_flag(contBottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(contBottom, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_pad_hor(contBottom, 8, 0);

    lblTemp = lv_label_create(contBottom);
    lv_obj_add_style(lblTemp, &styleDetail, 0);
    lv_obj_set_style_text_color(lblTemp, lv_color_hex(0xFFAA00), 0);
    lv_obj_align(lblTemp, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(lblTemp, "--\xC2\xB0""C");

    lblVolt = lv_label_create(contBottom);
    lv_obj_add_style(lblVolt, &styleDetail, 0);
    lv_obj_set_style_text_color(lblVolt, lv_color_hex(0x00D4FF), 0);
    lv_obj_align(lblVolt, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lblVolt, "--V");

    lblCurr = lv_label_create(contBottom);
    lv_obj_add_style(lblCurr, &styleDetail, 0);
    lv_obj_set_style_text_color(lblCurr, lv_color_hex(0x39FF14), 0);
    lv_obj_align(lblCurr, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(lblCurr, "--A");
}

static void updateUI() {
    char buf[32];
    unsigned long age = millis() - bms.lastUpdate;
    bool dataFresh = bms.online && age < 10000;

    if (!bleConnected) {
        lv_label_set_text(lblStatus, "BLE X");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0xFF3333), 0);
    } else if (!dataFresh) {
        lv_label_set_text(lblStatus, "WAIT");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0xFFAA00), 0);
    } else {
        lv_label_set_text(lblStatus, "LIVE");
        lv_obj_set_style_text_color(lblStatus, lv_color_hex(0x39FF14), 0);
    }

    if (!dataFresh && !bleConnected) return;

    float absPower = fabs(bms.power);
    snprintf(buf, sizeof(buf), "%.1f", absPower);
    lv_label_set_text(lblPower, buf);

    if (bms.current > 0.05f) {
        lv_obj_set_style_text_color(lblPower, lv_color_hex(0x39FF14), 0);
        lv_obj_set_style_border_color(contPower, lv_color_hex(0x39FF14), 0);
        lv_obj_remove_style(barPower, &styleBarIndOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarIndGreen, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lblMode, lv_color_hex(0x39FF14), 0);
#ifdef USE_CN_FONT
        lv_label_set_text(lblMode, "\xe2\x96\xb2 \xe5\x85\x85\xe7\x94\xb5");
#else
        lv_label_set_text(lblMode, "CHARGE");
#endif
    } else if (bms.current < -0.05f) {
        lv_obj_set_style_text_color(lblPower, lv_color_hex(0xFF6600), 0);
        lv_obj_set_style_border_color(contPower, lv_color_hex(0xFF6600), 0);
        lv_obj_remove_style(barPower, &styleBarIndGreen, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarIndOrange, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lblMode, lv_color_hex(0xFF6600), 0);
#ifdef USE_CN_FONT
        lv_label_set_text(lblMode, "\xe2\x96\xbc \xe6\x94\xbe\xe7\x94\xb5");
#else
        lv_label_set_text(lblMode, "DISCHARGE");
#endif
    } else {
        lv_obj_set_style_text_color(lblPower, lv_color_hex(0x00D4FF), 0);
        lv_obj_set_style_border_color(contPower, lv_color_hex(0x00D4FF), 0);
        lv_obj_remove_style(barPower, &styleBarIndGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barPower, &styleBarIndOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barPower, &styleBarIndCyan, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lblMode, lv_color_hex(0x00D4FF), 0);
#ifdef USE_CN_FONT
        lv_label_set_text(lblMode, "\xe2\x97\x86 \xe5\xbe\x85\xe6\x9c\xba");
#else
        lv_label_set_text(lblMode, "IDLE");
#endif
    }

    lv_bar_set_value(barPower, (int32_t)absPower, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%d", bms.soc);
    lv_label_set_text(lblSoc, buf);

    if (bms.soc > 60) {
        lv_obj_set_style_text_color(lblSoc, lv_color_hex(0x39FF14), 0);
        lv_obj_remove_style(barSoc, &styleBarIndCyan, LV_PART_INDICATOR);
        lv_obj_remove_style(barSoc, &styleBarIndOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barSoc, &styleBarIndGreen, LV_PART_INDICATOR);
    } else if (bms.soc > 20) {
        lv_obj_set_style_text_color(lblSoc, lv_color_hex(0x00D4FF), 0);
        lv_obj_remove_style(barSoc, &styleBarIndGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barSoc, &styleBarIndOrange, LV_PART_INDICATOR);
        lv_obj_add_style(barSoc, &styleBarIndCyan, LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_text_color(lblSoc, lv_color_hex(0xFF6600), 0);
        lv_obj_remove_style(barSoc, &styleBarIndGreen, LV_PART_INDICATOR);
        lv_obj_remove_style(barSoc, &styleBarIndCyan, LV_PART_INDICATOR);
        lv_obj_add_style(barSoc, &styleBarIndOrange, LV_PART_INDICATOR);
    }

    lv_bar_set_value(barSoc, bms.soc, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%.1f / %.1f Ah", bms.remainCap, bms.nominalCap);
    lv_label_set_text(lblCapDetail, buf);

    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", bms.temp1);
    lv_label_set_text(lblTemp, buf);

    snprintf(buf, sizeof(buf), "%.2fV", bms.voltage);
    lv_label_set_text(lblVolt, buf);

    snprintf(buf, sizeof(buf), "%.2fA", bms.current);
    lv_label_set_text(lblCurr, buf);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nJKBMS BLE Monitor Starting...");

    memset(&bms, 0, sizeof(bms));
    bms.temp1 = -999;
    bms.temp2 = -999;

    initDisplay();
    initStyles();
    createUI();

    Serial.println("UI ready, connecting BLE...");
    lastReconnTime = millis();
}

void loop() {
    lv_timer_handler();

    if (!bleConnected && (millis() - lastReconnTime > RECONNECT_MS)) {
        lastReconnTime = millis();
        Serial.println("Attempting BLE reconnect...");
        connectBMS();
    }

    if (bleConnected && (millis() - lastCmdTime > CMD_INTERVAL)) {
        lastCmdTime = millis();
        sendCmd(CMD_CELL_INFO);
    }

    static unsigned long lastUI = 0;
    if (millis() - lastUI > UI_INTERVAL) {
        lastUI = millis();
        updateUI();
    }

    delay(5);
}
