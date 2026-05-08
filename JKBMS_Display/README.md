# JKBMS 蓝牙监控显示器

基于 ESP32-32E + ST7789 2.8寸显示屏的 JKBMS 蓝牙监控方案

## 硬件需求

| 组件 | 型号/规格 | 数量 |
|------|-----------|------|
| 开发板 | ESP32-32E | 1 |
| 显示屏 | 2.8寸 ST7789P3 (240x320) | 1 |
| 连接线 | 杜邦线 | 若干 |

## 接线说明

### ESP32-32E 与 ST7789 显示屏连接 (官方引脚分配)

| ST7789引脚 | ESP32引脚 | 说明 |
|------------|-----------|------|
| VCC | 3.3V | 电源正 |
| GND | GND | 电源负 |
| CS | GPIO15 | 片选 (TFT_CS) |
| DC/RS | GPIO2 | 数据/命令选择 (TFT_RS) |
| RST | EN | 复位 (与开发板复位共享) |
| MOSI | GPIO13 | SPI数据 (TFT_MOSI) |
| SCK | GPIO14 | SPI时钟 (TFT_SCK) |
| MISO | GPIO12 | SPI读取 (TFT_MISO) |
| BL | GPIO21 | 背光控制 (高电平亮) |

## 软件依赖

在 ArduinoDroid 或 Arduino IDE 中安装以下库：

1. **NimBLE-Arduino** by h2zero
   - 版本: 1.4.0 或更高
   - 用于蓝牙低功耗通信

2. **TFT_eSPI** by Bodmer
   - 版本: 2.5.0 或更高
   - 用于显示屏驱动

## 安装步骤

### 1. 安装库

**ArduinoDroid 安装方法：**
1. 打开 ArduinoDroid
2. 点击菜单 → Sketch → Include Library → Manage Libraries
3. 搜索 "NimBLE-Arduino" 并安装
4. 搜索 "TFT_eSPI" 并安装

### 2. 配置 TFT_eSPI

**重要：** 需要修改 TFT_eSPI 库的配置文件：

1. 找到 TFT_eSPI 库的安装目录
   - ArduinoDroid: 通常在 `/sdcard/Arduino/libraries/TFT_eSPI/`

2. 备份并替换 `User_Setup.h` 文件
   - 将本项目中的 `User_Setup.h` 复制到 TFT_eSPI 目录

3. 或者编辑现有的 `User_Setup.h`，确保包含：
```cpp
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1   // EN引脚自动处理
#define TFT_BL   21   // 背光控制
#define SPI_FREQUENCY 40000000
```

### 3. 上传代码

1. 打开 `JKBMS_Display.ino`
2. 确认你的 BMS MAC 地址已正确设置：
```cpp
#define BMS_MAC_ADDRESS "98:DA:20:07:B9:00"
```
3. 选择开发板：
   - Tools → Board → ESP32 Arduino → ESP32 Dev Module
4. 选择正确的端口
5. 点击上传

## 功能特性

### 主界面布局 (240x320 竖屏)

```
┌─────────────────────────────┐
│  POWER          CHARGING    │  ← 功率区域 (110px)
│  1250 W                     │
│  52.5V  23.8A    [====] 83% │
├─────────────────────────────┤
│  CAPACITY       Cycles: 45  │  ← 容量区域 (90px)
│  125.5 / 280 Ah             │
│  [========== 45% =========] │
├─────────────────────────────┤
│  TEMPERATURE                │  ← 温度区域 (60px)
│  T1:25.5C T2:26.1C MOS:32C  │
├─────────────────────────────┤
│  ●ONLINE  CHG DSG BAL       │  ← 状态区域 (60px)
│  16S  3.285V  d0.012V       │
└─────────────────────────────┘
```

### 显示内容

1. **功率区域** (主视觉)
   - 实时功率 (W/kW)
   - 充放电状态指示
   - 电压、电流
   - 功率进度条
   - SOC百分比

2. **容量区域**
   - 剩余容量 / 总容量 (Ah)
   - 大进度条显示SOC
   - 循环次数

3. **温度区域**
   - 温度传感器 T1、T2
   - MOS管温度
   - 温度可视化条

4. **状态区域**
   - 蓝牙连接状态
   - 充电/放电MOS状态
   - 均衡状态
   - 错误指示
   - 单体统计信息

### 电竞风格UI特点

- **黑底高对比度**：黑色背景 + 亮色文字
- **动态进度条**：功率、SOC、温度可视化
- **状态颜色编码**：
  - 绿色 = 正常/充电
  - 红色 = 放电/警告/错误
  - 黄色 = 注意/均衡
  - 青色 = 主色调
- **圆角设计**：现代化UI元素

## 故障排除

### 显示屏不亮

1. 检查接线是否正确
2. 确认背光 LED 引脚已连接
3. 尝试修改 `User_Setup.h` 中的 `TFT_INVERSION_ON/OFF`
4. 检查 SPI 频率是否过高，尝试降低到 20MHz

### 蓝牙无法连接

1. 确认 BMS MAC 地址正确
2. 确保 BMS 已开启蓝牙
3. 检查串口输出，查看是否扫描到设备
4. 尝试重启 ESP32

### 数据显示异常

1. 检查 CRC 校验错误信息
2. 确认 BMS 协议版本兼容 (JK02_32S)
3. 查看串口输出的原始数据

### 屏幕颜色异常

1. 修改 `TFT_RGB_ORDER`：
   - `TFT_RGB` 或 `TFT_BGR`
2. 尝试开启/关闭 `TFT_INVERSION_ON`

## 调试

开启串口调试 (115200 baud)：
```cpp
#define DEBUG_ENABLED true
```

查看调试信息：
- 连接状态
- 接收到的数据帧
- 解析后的数值

## 自定义配置

### 修改刷新间隔
```cpp
#define DISPLAY_UPDATE_INTERVAL 500  // 显示刷新 (ms)
#define BMS_POLL_INTERVAL 2000       // 数据请求间隔 (ms)
```

### 修改功率进度条最大值
```cpp
// 在 drawPowerArea() 函数中
powerPercent = min(bmsData.batteryPower / 5000.0 * 100.0, 100.0);
// 5000 改为你的电池最大功率
```

## 参考项目

- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) - JKBMS协议文档
- [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms) - NimBLE实现参考

## 许可证

MIT License
