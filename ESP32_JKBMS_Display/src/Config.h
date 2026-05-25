#ifndef CONFIG_H
#define CONFIG_H

namespace Config {
    const char* DEVICE_NAME = "ESP32-JKBMS-Display";
    const char* FIRMWARE_VERSION = "1.0.0";
    const char* BUILD_DATE = __DATE__;
    const char* BUILD_TIME = __TIME__;
    
    namespace Bluetooth {
        const char* BMS_DEVICE_NAME = "JK_BD4A24S10P";
        const char* BMS_MAC_ADDRESS = "98:da:20:07:b9:00";
        const char* PROTOCOL_VERSION = "JK02_32S";
        const char* SERVICE_UUID = "FFE0";
        const char* CHARACTERISTIC_UUID = "FFE1";
        const uint32_t SCAN_TIMEOUT = 10000;
        const uint8_t MAX_RECONNECT_ATTEMPTS = 3;
    }
    
    namespace Display {
        const uint16_t WIDTH = 240;
        const uint16_t HEIGHT = 320;
        const uint8_t ROTATION = 1;
        const uint32_t SPI_FREQ = 40000000;
        const uint32_t SPI_READ_FREQ = 20000000;
        const uint16_t REFRESH_RATE = 100;
    }
    
    namespace UI {
        const float MAX_POWER = 5000.0f;
        const uint8_t POWER_LOW_THRESHOLD = 33;
        const uint8_t POWER_MED_THRESHOLD = 66;
        const uint8_t ANIMATION_SPEED = 15;
        const uint8_t CARD_CORNER_RADIUS = 8;
        const uint8_t CARD_SHADOW_OFFSET = 3;
    }
    
    namespace Debug {
        const bool ENABLE_SERIAL = true;
        const uint32_t SERIAL_BAUD = 115200;
        const bool VERBOSE_LOGGING = true;
    }
}

#endif
