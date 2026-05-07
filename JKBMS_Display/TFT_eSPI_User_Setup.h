// TFT_eSPI 用户配置文件
// 适用于 ESP32-32E + 2.8寸 ST7789P3 显示屏

#define USER_SETUP_INFO "ESP32_ST7789_240x320"

// 驱动芯片选择
#define ST7789_DRIVER

// 屏幕尺寸
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// SPI 引脚配置 (根据你的接线修改)
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

// 背光引脚 (可选，-1表示不使用)
#define TFT_BL   -1

// SPI 频率
#define SPI_FREQUENCY  40000000

// 其他选项
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
