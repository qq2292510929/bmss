// ============================================================
// TFT_eSPI 配置文件 - 用于 ESP32 + 2.8寸 ST7789P3
// ============================================================

#define USER_SETUP_INFO "ESP32_ST7789_320x240"

// 驱动选择 - ST7789
#define ST7789_DRIVER

// 屏幕尺寸 (2.8寸常见分辨率)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ============================================================
// ESP32-32E 板载 LCD 引脚定义 (E32R28T-1 开发板)
// 来源: LCDWIKI E32R28T-1 Arduino Demo Instructions
// ============================================================
#define TFT_MISO 12   // IO12 - LCD SPI MISO
#define TFT_MOSI 13   // IO13 - LCD SPI MOSI
#define TFT_SCLK 14   // IO14 - LCD SPI SCK
#define TFT_CS   15   // IO15 - LCD 片选 (低电平有效)
#define TFT_DC   2    // IO2  - LCD RS/DC (高=data, 低=command)
#define TFT_RST  -1   // EN   - 与ESP32复位共享,设为-1由库自动处理

// 背光引脚 (高电平点亮)
#define TFT_BL   21   // IO21 - LCD 背光控制

// SPI 频率
#define SPI_FREQUENCY  40000000

// 颜色顺序 (如果颜色不对,尝试切换这个)
#define TFT_RGB_ORDER TFT_RGB

// 加载字体
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT
