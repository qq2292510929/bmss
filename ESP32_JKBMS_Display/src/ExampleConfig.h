#ifndef EXAMPLE_CONFIG_H
#define EXAMPLE_CONFIG_H

const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASSWORD = "your_wifi_password";

const char* OTA_PASSWORD = "your_ota_password";

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 8 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

const float MAX_BATTERY_CAPACITY = 100.0f;
const float LOW_BATTERY_THRESHOLD = 20.0f;
const float CRITICAL_BATTERY_THRESHOLD = 10.0f;

const float MAX_CHARGE_CURRENT = 50.0f;
const float MAX_DISCHARGE_CURRENT = 100.0f;
const float MAX_CHARGE_POWER = 2000.0f;
const float MAX_DISCHARGE_POWER = 5000.0f;

const uint8_t CELL_COUNT = 16;
const float CELL_MIN_VOLTAGE = 2.5f;
const float CELL_MAX_VOLTAGE = 4.2f;
const float CELL_NOMINAL_VOLTAGE = 3.3f;

const uint8_t TEMP_SENSOR_COUNT = 2;
const int16_t TEMP_MIN = -20;
const int16_t TEMP_MAX = 60;
const int16_t TEMP_WARNING = 45;

const uint16_t SCREEN_REFRESH_RATE = 100;
const uint16_t BLUETOOTH_SCAN_INTERVAL = 5000;
const uint16_t DATA_LOG_INTERVAL = 60000;

const bool ENABLE_DEBUG_MODE = false;
const bool ENABLE_VERBOSE_LOGGING = false;
const bool ENABLE_PERFORMANCE_STATS = true;

const uint8_t ALERT_LED_PIN = LED_BUILTIN;
const bool ENABLE_ALERTS = true;

const uint32_t WATCHDOG_TIMEOUT = 30000;

const char* DEVICE_LOCATION = "车库";
const char* BATTERY_TYPE = "LiFePO4";

#endif
