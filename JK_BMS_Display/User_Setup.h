/***************************************************
 * TFT_eSPI User Setup for ESP32-32E + ST7789 2.8" 320x240
 * Copy this file to your Arduino libraries/TFT_eSPI folder
 * OR use the "Setup135_ST7789" as reference
 ***************************************************/

// Driver selection
#define ST7789_DRIVER

// Display dimensions
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Color order
#define TFT_RGB_ORDER TFT_RGB

// ESP32 Pin definitions
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

// Backlight (optional, set to -1 if not used)
#define TFT_BL   4
#define TFT_BACKLIGHT_ON HIGH

// SPI frequency
#define SPI_FREQUENCY  40000000  // 40MHz for ST7789

// SPI read frequency (lower for reliability)
#define SPI_READ_FREQUENCY  20000000

// Touch controller (not used)
#define TOUCH_CS -1
