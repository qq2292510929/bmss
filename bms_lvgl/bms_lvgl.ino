/*
 * JK BMS 蓝牙监控仪表盘 - LVGL 赛车战斗模式
 * 硬件: ESP32-32E + ST7789 2.8寸 320x240
 * BMS:  JK_BD4A24S10P (JK02_32S协议)
 * 风格: 全圆角、高对比度、赛车红+深灰+亮青
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <SPI.h>

// LVGL
#include <lvgl.h>
#include <TFT_eSPI.h>

// ===================== 引脚配置 =====================
#define TFT_BL    21

// ===================== BLE 配置 =====================
#define BMS_MAC  "98:da:20:07:b9:00"
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

// ===================== 屏幕参数 =====================
#define SCREEN_W 320
#define SCREEN_H 240

// ===================== LVGL 显示缓冲 =====================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 10];
static lv_color_t buf2[SCREEN_W * 10];
TFT_eSPI tft = TFT_eSPI();

// ===================== BMS 数据结构 =====================
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

// ===================== BLE 全局变量 =====================
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pWriteChar = nullptr;
BLERemoteCharacteristic* pNotifyChar = nullptr;
bool bleConnected = false;
bool doConnect = false;
BLEAdvertisedDevice* advDevice = nullptr;
uint8_t frameBuf[MAX_FRAME_SIZE];
int framePos = 0;
bool frameStarted = false;
unsigned long lastCommandTime = 0;
unsigned long lastScanTime = 0;
const unsigned long COMMAND_INTERVAL = 8000;
const unsigned long SCAN_INTERVAL = 5000;
const unsigned long DATA_TIMEOUT = 30000;

// ===================== UI 对象 =====================
lv_obj_t *scr_main;
lv_obj_t *lbl_power;
lv_obj_t *lbl_power_unit;
lv_obj_t *lbl_status;
lv_obj_t *lbl_soc;
lv_obj_t *bar_soc;
lv_obj_t *lbl_cap;
lv_obj_t *lbl_temp;
lv_obj_t *lbl_volt;
lv_obj_t *lbl_curr;
lv_obj_t *lbl_ble;
lv_obj_t *panel_power;
lv_obj_t *panel_soc;
lv_obj_t *card_temp;
lv_obj_t *card_volt;
lv_obj_t *card_curr;

// 动画对象
lv_anim_t anim_pulse;
lv_obj_t *pulse_bar;

// ===================== 颜色定义 (赛车模式) =====================
#define C_BG            lv_color_hex(0x0D0D0D)
#define C_PANEL         lv_color_hex(0x1A1A1A)
#define C_PANEL_BORDER  lv_color_hex(0x2D2D2D)
#define C_RACING_RED    lv_color_hex(0xE63946)
#define C_RACING_RED_D  lv_color_hex(0x8B0000)
#define C_CYAN          lv_color_hex(0x00F5FF)
#define C_CYAN_D        lv_color_hex(0x008B8B)
#define C_YELLOW        lv_color_hex(0xFFD700)
#define C_WHITE         lv_color_hex(0xFFFFFF)
#define C_GRAY          lv_color_hex(0x888888)
#define C_DARK_GRAY     lv_color_hex(0x444444)
#define C_GREEN         lv_color_hex(0x00FF7F)

// ===================== CRC 计算 =====================
uint8_t calcCRC(const uint8_t* data, uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) crc += data[i];
  return crc;
}

// ===================== 发送BLE命令 =====================
void sendBMSCommand(uint8_t cmd) {
  if (!pWriteChar) return;
  uint8_t cmdFrame[20] = {
    0xAA, 0x55, 0x90, 0xEB, cmd, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  cmdFrame[19] = calcCRC(cmdFrame, 19);
  pWriteChar->writeValue(cmdFrame, 20, true);
}

// ===================== 解析JK02_32S帧 =====================
void parseCellInfoFrame() {
  if (framePos < MIN_FRAME_SIZE) return;
  uint8_t crc = calcCRC(frameBuf, framePos - 1);
  if (crc != frameBuf[framePos - 1]) return;
  if (frameBuf[4] != FRAME_TYPE_CELL_INFO) return;

  bms.voltage = ((uint32_t)frameBuf[121] << 24 | (uint32_t)frameBuf[120] << 16 |
                 (uint32_t)frameBuf[119] << 8 | (uint32_t)frameBuf[118]) * 0.001f;
  bms.power = ((uint32_t)frameBuf[125] << 24 | (uint32_t)frameBuf[124] << 16 |
               (uint32_t)frameBuf[123] << 8 | (uint32_t)frameBuf[122]) * 0.001f;
  int32_t rawCurrent = (int32_t)((uint32_t)frameBuf[129] << 24 | (uint32_t)frameBuf[128] << 16 |
                                 (uint32_t)frameBuf[127] << 8 | (uint32_t)frameBuf[126]);
  bms.current = rawCurrent * 0.001f;
  int16_t rawT1 = (int16_t)((uint16_t)frameBuf[131] << 8 | frameBuf[130]);
  bms.temp1 = rawT1 * 0.1f;
  bms.temp2 = (int16_t)((uint16_t)frameBuf[133] << 8 | frameBuf[132]) * 0.1f;
  bms.soc = frameBuf[141];
  bms.capacity_remain = ((uint32_t)frameBuf[145] << 24 | (uint32_t)frameBuf[144] << 16 |
                         (uint32_t)frameBuf[143] << 8 | (uint32_t)frameBuf[142]) * 0.001f;
  bms.capacity_nominal = ((uint32_t)frameBuf[149] << 24 | (uint32_t)frameBuf[148] << 16 |
                          (uint32_t)frameBuf[147] << 8 | (uint32_t)frameBuf[146]) * 0.001f;
  bms.isCharging = (bms.current > 0.05f);
  bms.isDischarging = (bms.current < -0.05f);
  bms.dataValid = true;
  bms.lastUpdate = millis();
}

// ===================== BLE 通知回调 =====================
void notifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (length >= 4 && pData[0] == FRAME_HEADER_0 && pData[1] == FRAME_HEADER_1 &&
      pData[2] == FRAME_HEADER_2 && pData[3] == FRAME_HEADER_3) {
    framePos = 0;
    frameStarted = true;
  }
  if (frameStarted) {
    for (size_t i = 0; i < length; i++) {
      if (framePos < MAX_FRAME_SIZE) frameBuf[framePos++] = pData[i];
      if (framePos >= MIN_FRAME_SIZE) {
        frameStarted = false;
        parseCellInfoFrame();
        break;
      }
    }
  }
}

// ===================== BLE 回调类 =====================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    bleConnected = true;
    lv_label_set_text(lbl_ble, "ON");
    lv_obj_set_style_text_color(lbl_ble, C_GREEN, 0);
  }
  void onDisconnect(BLEClient* pclient) {
    bleConnected = false;
    doConnect = false;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
    lv_label_set_text(lbl_ble, "OFF");
    lv_obj_set_style_text_color(lbl_ble, C_RACING_RED, 0);
  }
};

class MyScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().toString() == BMS_MAC) {
      BLEDevice::getScan()->stop();
      if (advDevice) delete advDevice;
      advDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

bool connectToBMS() {
  if (!advDevice) return false;
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  if (!pClient->connect(advDevice)) {
    delete pClient;
    pClient = nullptr;
    return false;
  }
  BLERemoteService* pService = pClient->getService(serviceUUID);
  if (!pService) {
    pClient->disconnect();
    return false;
  }
  std::map<std::string, BLERemoteCharacteristic*>* charMap = pService->getCharacteristics();
  for (auto& kv : *charMap) {
    BLERemoteCharacteristic* c = kv.second;
    if (c->canWrite() && !pWriteChar) pWriteChar = c;
    if (c->canNotify() && !pNotifyChar) pNotifyChar = c;
  }
  if (!pNotifyChar) {
    pClient->disconnect();
    return false;
  }
  pNotifyChar->registerForNotify(notifyCallback);
  if (!pWriteChar && pNotifyChar && pNotifyChar->canWrite()) pWriteChar = pNotifyChar;
  delay(500);
  sendBMSCommand(CMD_DEVICE_INFO);
  delay(500);
  sendBMSCommand(CMD_CELL_INFO);
  lastCommandTime = millis();
  return true;
}

// ===================== LVGL 显示刷新 =====================
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

// ===================== 创建圆角卡片 =====================
lv_obj_t* create_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t accent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, C_PANEL, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, C_PANEL_BORDER, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // 顶部强调条
  lv_obj_t *bar = lv_obj_create(card);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, w, 3);
  lv_obj_set_style_bg_color(bar, accent, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  return card;
}

// ===================== 更新UI数据 =====================
void update_ui() {
  if (!bms.dataValid) return;

  // 功率
  float absP = fabs(bms.power);
  char buf[32];
  if (absP >= 1000.0f) {
    sprintf(buf, "%s%.0f", bms.isCharging ? "+" : (bms.isDischarging ? "-" : ""), absP);
  } else if (absP >= 100.0f) {
    sprintf(buf, "%s%.1f", bms.isCharging ? "+" : (bms.isDischarging ? "-" : ""), absP);
  } else {
    sprintf(buf, "%s%.2f", bms.isCharging ? "+" : (bms.isDischarging ? "-" : ""), absP);
  }
  lv_label_set_text(lbl_power, buf);

  // 功率颜色
  if (bms.isCharging) {
    lv_obj_set_style_text_color(lbl_power, C_CYAN, 0);
    lv_obj_set_style_text_color(lbl_power_unit, C_CYAN, 0);
    lv_obj_set_style_border_color(panel_power, C_CYAN, 0);
  } else if (bms.isDischarging) {
    lv_obj_set_style_text_color(lbl_power, C_RACING_RED, 0);
    lv_obj_set_style_text_color(lbl_power_unit, C_RACING_RED, 0);
    lv_obj_set_style_border_color(panel_power, C_RACING_RED, 0);
  } else {
    lv_obj_set_style_text_color(lbl_power, C_GRAY, 0);
    lv_obj_set_style_text_color(lbl_power_unit, C_GRAY, 0);
    lv_obj_set_style_border_color(panel_power, C_PANEL_BORDER, 0);
  }

  // 状态
  if (bms.isCharging) {
    lv_label_set_text(lbl_status, "CHARGE");
    lv_obj_set_style_text_color(lbl_status, C_CYAN, 0);
  } else if (bms.isDischarging) {
    lv_label_set_text(lbl_status, "DISCHG");
    lv_obj_set_style_text_color(lbl_status, C_RACING_RED, 0);
  } else {
    lv_label_set_text(lbl_status, "STBY");
    lv_obj_set_style_text_color(lbl_status, C_GRAY, 0);
  }

  // SOC
  sprintf(buf, "%d%%", bms.soc);
  lv_label_set_text(lbl_soc, buf);
  lv_bar_set_value(bar_soc, bms.soc, LV_ANIM_ON);

  // SOC颜色
  lv_color_t soc_color;
  if (bms.soc > 60) soc_color = C_CYAN;
  else if (bms.soc > 30) soc_color = C_YELLOW;
  else if (bms.soc > 15) soc_color = lv_color_hex(0xFF8C00);
  else soc_color = C_RACING_RED;
  lv_obj_set_style_text_color(lbl_soc, soc_color, 0);
  lv_obj_set_style_bg_color(bar_soc, soc_color, LV_PART_INDICATOR);

  // 容量
  sprintf(buf, "%.1f/%.1fAh", bms.capacity_remain, bms.capacity_nominal);
  lv_label_set_text(lbl_cap, buf);

  // 温度
  sprintf(buf, "%.1f", bms.temp1);
  lv_label_set_text(lbl_temp, buf);

  // 电压
  sprintf(buf, "%.1f", bms.voltage);
  lv_label_set_text(lbl_volt, buf);

  // 电流
  sprintf(buf, "%.2f", bms.current);
  lv_label_set_text(lbl_curr, buf);
}

// ===================== 脉冲动画回调 =====================
static void pulse_anim_cb(void *var, int32_t v) {
  lv_obj_t *obj = (lv_obj_t *)var;
  lv_obj_set_style_bg_opa(obj, v, 0);
}

// ===================== 创建主界面 =====================
void create_main_ui() {
  scr_main = lv_scr_act();
  lv_obj_set_style_bg_color(scr_main, C_BG, 0);

  // ---- 顶部装饰条 ----
  lv_obj_t *top_bar = lv_obj_create(scr_main);
  lv_obj_set_pos(top_bar, 0, 0);
  lv_obj_set_size(top_bar, SCREEN_W, 3);
  lv_obj_set_style_bg_color(top_bar, C_RACING_RED, 0);
  lv_obj_set_style_radius(top_bar, 0, 0);
  lv_obj_set_style_border_width(top_bar, 0, 0);
  lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

  // ---- 标题栏 ----
  lv_obj_t *title_box = lv_obj_create(scr_main);
  lv_obj_set_pos(title_box, 8, 8);
  lv_obj_set_size(title_box, 200, 24);
  lv_obj_set_style_bg_color(title_box, C_PANEL, 0);
  lv_obj_set_style_radius(title_box, 6, 0);
  lv_obj_set_style_border_width(title_box, 1, 0);
  lv_obj_set_style_border_color(title_box, C_PANEL_BORDER, 0);
  lv_obj_set_style_pad_all(title_box, 0, 0);
  lv_obj_clear_flag(title_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(title_box);
  lv_label_set_text(lbl_title, "BMS MONITOR");
  lv_obj_set_style_text_color(lbl_title, C_CYAN, 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t *lbl_name = lv_label_create(title_box);
  lv_label_set_text(lbl_name, BMS_NAME);
  lv_obj_set_style_text_color(lbl_name, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl_name, LV_ALIGN_RIGHT_MID, -8, 0);

  // BLE状态
  lv_obj_t *ble_box = lv_obj_create(scr_main);
  lv_obj_set_pos(ble_box, SCREEN_W - 52, 8);
  lv_obj_set_size(ble_box, 44, 24);
  lv_obj_set_style_bg_color(ble_box, C_PANEL, 0);
  lv_obj_set_style_radius(ble_box, 6, 0);
  lv_obj_set_style_border_width(ble_box, 1, 0);
  lv_obj_set_style_border_color(ble_box, C_PANEL_BORDER, 0);
  lv_obj_set_style_pad_all(ble_box, 0, 0);
  lv_obj_clear_flag(ble_box, LV_OBJ_FLAG_SCROLLABLE);

  lbl_ble = lv_label_create(ble_box);
  lv_label_set_text(lbl_ble, "OFF");
  lv_obj_set_style_text_color(lbl_ble, C_RACING_RED, 0);
  lv_obj_set_style_text_font(lbl_ble, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_ble, LV_ALIGN_CENTER, 0, 0);

  // ---- 主功率面板 ----
  panel_power = lv_obj_create(scr_main);
  lv_obj_set_pos(panel_power, 8, 38);
  lv_obj_set_size(panel_power, 304, 108);
  lv_obj_set_style_bg_color(panel_power, C_PANEL, 0);
  lv_obj_set_style_radius(panel_power, 10, 0);
  lv_obj_set_style_border_width(panel_power, 2, 0);
  lv_obj_set_style_border_color(panel_power, C_PANEL_BORDER, 0);
  lv_obj_set_style_pad_all(panel_power, 0, 0);
  lv_obj_clear_flag(panel_power, LV_OBJ_FLAG_SCROLLABLE);

  // 脉冲动画条
  pulse_bar = lv_obj_create(panel_power);
  lv_obj_set_pos(pulse_bar, 10, 4);
  lv_obj_set_size(pulse_bar, 40, 2);
  lv_obj_set_style_bg_color(pulse_bar, C_RACING_RED, 0);
  lv_obj_set_style_radius(pulse_bar, 1, 0);
  lv_obj_set_style_border_width(pulse_bar, 0, 0);
  lv_obj_clear_flag(pulse_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(pulse_bar, LV_OPA_0, 0);

  // 功率标签
  lv_obj_t *lbl_p_title = lv_label_create(panel_power);
  lv_label_set_text(lbl_p_title, "POWER");
  lv_obj_set_style_text_color(lbl_p_title, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_p_title, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl_p_title, LV_ALIGN_TOP_LEFT, 12, 10);

  // 功率大数字
  lbl_power = lv_label_create(panel_power);
  lv_label_set_text(lbl_power, "---");
  lv_obj_set_style_text_color(lbl_power, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl_power, LV_ALIGN_CENTER, -10, 0);

  // 单位 W
  lbl_power_unit = lv_label_create(panel_power);
  lv_label_set_text(lbl_power_unit, "W");
  lv_obj_set_style_text_color(lbl_power_unit, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_power_unit, &lv_font_montserrat_20, 0);
  lv_obj_align_to(lbl_power_unit, lbl_power, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);

  // 状态标签
  lbl_status = lv_label_create(panel_power);
  lv_label_set_text(lbl_status, "STBY");
  lv_obj_set_style_text_color(lbl_status, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);

  // ---- SOC 区域 ----
  panel_soc = lv_obj_create(scr_main);
  lv_obj_set_pos(panel_soc, 8, 152);
  lv_obj_set_size(panel_soc, 200, 34);
  lv_obj_set_style_bg_color(panel_soc, C_PANEL, 0);
  lv_obj_set_style_radius(panel_soc, 8, 0);
  lv_obj_set_style_border_width(panel_soc, 1, 0);
  lv_obj_set_style_border_color(panel_soc, C_PANEL_BORDER, 0);
  lv_obj_set_style_pad_all(panel_soc, 0, 0);
  lv_obj_clear_flag(panel_soc, LV_OBJ_FLAG_SCROLLABLE);

  lbl_soc = lv_label_create(panel_soc);
  lv_label_set_text(lbl_soc, "--%");
  lv_obj_set_style_text_color(lbl_soc, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_soc, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_soc, LV_ALIGN_LEFT_MID, 10, 0);

  // SOC进度条
  bar_soc = lv_bar_create(panel_soc);
  lv_obj_set_size(bar_soc, 90, 10);
  lv_obj_align(bar_soc, LV_ALIGN_CENTER, 10, 0);
  lv_bar_set_range(bar_soc, 0, 100);
  lv_bar_set_value(bar_soc, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar_soc, C_DARK_GRAY, 0);
  lv_obj_set_style_radius(bar_soc, 5, 0);
  lv_obj_set_style_bg_color(bar_soc, C_CYAN, LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar_soc, 5, LV_PART_INDICATOR);

  // 容量文字
  lbl_cap = lv_label_create(scr_main);
  lv_label_set_text(lbl_cap, "--/--Ah");
  lv_obj_set_style_text_color(lbl_cap, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_cap, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(lbl_cap, 216, 160);

  // ---- 底部三个卡片 ----
  // 温度卡片 - 青色
  card_temp = create_card(scr_main, 8, 194, 96, 42, C_CYAN);
  lv_obj_t *lbl_t_name = lv_label_create(card_temp);
  lv_label_set_text(lbl_t_name, "TEMP");
  lv_obj_set_style_text_color(lbl_t_name, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_t_name, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl_t_name, LV_ALIGN_TOP_LEFT, 8, 8);

  lbl_temp = lv_label_create(card_temp);
  lv_label_set_text(lbl_temp, "--");
  lv_obj_set_style_text_color(lbl_temp, C_WHITE, 0);
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_LEFT, 8, -6);

  lv_obj_t *lbl_t_unit = lv_label_create(card_temp);
  lv_label_set_text(lbl_t_unit, "C");
  lv_obj_set_style_text_color(lbl_t_unit, C_CYAN, 0);
  lv_obj_set_style_text_font(lbl_t_unit, &lv_font_montserrat_10, 0);
  lv_obj_align_to(lbl_t_unit, lbl_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 2);

  // 电压卡片 - 黄色
  card_volt = create_card(scr_main, 112, 194, 96, 42, C_YELLOW);
  lv_obj_t *lbl_v_name = lv_label_create(card_volt);
  lv_label_set_text(lbl_v_name, "VOLT");
  lv_obj_set_style_text_color(lbl_v_name, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_v_name, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl_v_name, LV_ALIGN_TOP_LEFT, 8, 8);

  lbl_volt = lv_label_create(card_volt);
  lv_label_set_text(lbl_volt, "--");
  lv_obj_set_style_text_color(lbl_volt, C_WHITE, 0);
  lv_obj_set_style_text_font(lbl_volt, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_volt, LV_ALIGN_BOTTOM_LEFT, 8, -6);

  lv_obj_t *lbl_v_unit = lv_label_create(card_volt);
  lv_label_set_text(lbl_v_unit, "V");
  lv_obj_set_style_text_color(lbl_v_unit, C_YELLOW, 0);
  lv_obj_set_style_text_font(lbl_v_unit, &lv_font_montserrat_10, 0);
  lv_obj_align_to(lbl_v_unit, lbl_volt, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 2);

  // 电流卡片 - 红色
  card_curr = create_card(scr_main, 216, 194, 96, 42, C_RACING_RED);
  lv_obj_t *lbl_c_name = lv_label_create(card_curr);
  lv_label_set_text(lbl_c_name, "CURR");
  lv_obj_set_style_text_color(lbl_c_name, C_GRAY, 0);
  lv_obj_set_style_text_font(lbl_c_name, &lv_font_montserrat_10, 0);
  lv_obj_align(lbl_c_name, LV_ALIGN_TOP_LEFT, 8, 8);

  lbl_curr = lv_label_create(card_curr);
  lv_label_set_text(lbl_curr, "--");
  lv_obj_set_style_text_color(lbl_curr, C_WHITE, 0);
  lv_obj_set_style_text_font(lbl_curr, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_curr, LV_ALIGN_BOTTOM_LEFT, 8, -6);

  lv_obj_t *lbl_c_unit = lv_label_create(card_curr);
  lv_label_set_text(lbl_c_unit, "A");
  lv_obj_set_style_text_color(lbl_c_unit, C_RACING_RED, 0);
  lv_obj_set_style_text_font(lbl_c_unit, &lv_font_montserrat_10, 0);
  lv_obj_align_to(lbl_c_unit, lbl_curr, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, 2);

  // ---- 底部装饰条 ----
  lv_obj_t *bot_bar = lv_obj_create(scr_main);
  lv_obj_set_pos(bot_bar, 0, SCREEN_H - 3);
  lv_obj_set_size(bot_bar, SCREEN_W, 3);
  lv_obj_set_style_bg_color(bot_bar, C_RACING_RED, 0);
  lv_obj_set_style_radius(bot_bar, 0, 0);
  lv_obj_set_style_border_width(bot_bar, 0, 0);
  lv_obj_clear_flag(bot_bar, LV_OBJ_FLAG_SCROLLABLE);
}

// ===================== 脉冲动画 =====================
void start_pulse_anim() {
  lv_anim_init(&anim_pulse);
  lv_anim_set_var(&anim_pulse, pulse_bar);
  lv_anim_set_values(&anim_pulse, LV_OPA_0, LV_OPA_80);
  lv_anim_set_exec_cb(&anim_pulse, pulse_anim_cb);
  lv_anim_set_time(&anim_pulse, 600);
  lv_anim_set_playback_time(&anim_pulse, 600);
  lv_anim_set_repeat_count(&anim_pulse, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim_pulse);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  bms.dataValid = false;
  bms.soc = 0;
  bms.voltage = 0;
  bms.power = 0;
  bms.current = 0;
  bms.temp1 = 0;
  bms.temp2 = 0;
  bms.capacity_remain = 0;
  bms.capacity_nominal = 0;

  // TFT初始化
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // LVGL初始化
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_W * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // 创建UI
  create_main_ui();
  start_pulse_anim();

  // BLE初始化
  BLEDevice::init("");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyScanCallback());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(30, false);
}

// ===================== LOOP =====================
unsigned long lastUiUpdate = 0;

void loop() {
  lv_timer_handler();

  if (doConnect) {
    doConnect = false;
    if (!connectToBMS()) {
      doScan = true;
    }
  }

  if (bleConnected && (millis() - lastCommandTime > COMMAND_INTERVAL)) {
    sendBMSCommand(CMD_CELL_INFO);
    lastCommandTime = millis();
  }

  if (millis() - lastUiUpdate > 200) {
    update_ui();
    lastUiUpdate = millis();
  }

  if (!bleConnected && !doConnect) {
    if (millis() - lastScanTime > SCAN_INTERVAL) {
      BLEScan* pScan = BLEDevice::getScan();
      pScan->start(10, false);
      lastScanTime = millis();
    }
  }

  delay(5);
}
