// TFT_eSPI 用户配置文件
// 适用于 ESP32-32E + 2.8寸 ST7789P3 显示屏 (E32R28T-1 开发板)
// 根据官方引脚分配表配置

#define USER_SETUP_INFO "ESP32-32E_ST7789_240x320"

// 驱动芯片选择
#define ST7789_DRIVER

// 屏幕尺寸
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ============================================================
// SPI 引脚配置 (ESP32-32E 官方引脚分配)
// ============================================================
#define TFT_MISO 12   // IO12 - LCD SPI MISO
#define TFT_MOSI 13   // IO13 - LCD SPI MOSI
#define TFT_SCLK 14   // IO14 - LCD SPI SCK
#define TFT_CS   15   // IO15 - LCD 片选 (低电平有效)
#define TFT_DC   2    // IO2  - LCD 命令/数据选择 (高=数据, 低=命令)

// 复位引脚使用 EN 引脚 (与ESP32-32E主控共享复位)
// TFT_eSPI 中如果定义为 -1，需要在代码中手动控制复位
#define TFT_RST  -1

// 背光引脚 (高电平点亮)
#define TFT_BL   21   // IO21 - LCD 背光控制

// ============================================================
// SPI 频率
// ============================================================
#define SPI_FREQUENCY  40000000

// ============================================================
// 其他选项
// ============================================================
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
