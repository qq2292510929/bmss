# TFT_eSPI 配置说明

## 安装步骤

1. 在 ArduinoDroid 或 Arduino IDE 中安装 `TFT_eSPI` 库 (by Bodmer)

2. 找到 `User_Setup.h` 文件并修改:
   - 通常位于: `Arduino/libraries/TFT_eSPI/User_Setup.h`

## 需要修改的配置

```cpp
// 选择驱动
#define ST7789_DRIVER

// 显示屏尺寸 (2.8寸横屏)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// 颜色顺序
#define TFT_RGB_ORDER TFT_RGB

// 引脚配置 (ESP32-32E)
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

// 背光引脚 (可选PWM调光)
#define TFT_BL   21

// SPI频率
#define SPI_FREQUENCY  40000000

// 字体加载
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

// 平滑字体
#define SMOOTH_FONT
```

## 接线图

| ST7789P3 | ESP32-32E |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| CS       | GPIO 5    |
| RESET    | GPIO 17   |
| DC/RS    | GPIO 16   |
| SDI/MOSI | GPIO 23   |
| SCK      | GPIO 18   |
| LED      | 3.3V/PWM  |

## 依赖库

在 ArduinoDroid 中安装以下库:
1. `TFT_eSPI` - 显示驱动
2. `NimBLE-Arduino` - 低功耗蓝牙

## 上传设置

- 开发板: ESP32 Dev Module
- Flash Mode: QIO
- Flash Size: 4MB
- Partition Scheme: Default 4MB with spiffs
