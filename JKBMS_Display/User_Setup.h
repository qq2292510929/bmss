// User configuration file for TFT_eSPI library
// For ESP32-32E + ST7789 2.8" Display (240x320)

// Driver selection
#define ST7789_DRIVER

// Display dimensions
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

// SPI pins for ESP32-32E
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

// Backlight control (optional, set to -1 if not used)
#define TFT_BL   -1
#define TFT_BACKLIGHT_ON HIGH

// SPI frequency
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000

// Color order
#define TFT_RGB_ORDER TFT_RGB

// Inversion (may need adjustment for specific panel)
#define TFT_INVERSION_ON

// Offset for ST7789 (may vary by panel)
#define ST7789_240x320 1
