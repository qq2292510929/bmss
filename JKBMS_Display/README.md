# JK-BMS 电竞风蓝牙监控仪表盘

## 项目概述

基于 ESP32-32E + 2.8寸 ST7789P3 显示屏的 JKBMS 蓝牙监控仪表盘。
通过 BLE 连接 JKBMS (极空电池管理系统)，实时显示电池功率、电压、电流、容量、温度等关键信息。

## 硬件需求

| 组件 | 型号/规格 |
|------|----------|
| 开发板 | ESP32-32E |
| 显示屏 | 2.8寸 TFT LCD, ST7789P3 驱动 |
| 被监控设备 | JKBMS (极空电池管理系统) |

## 接线图

```
ESP32-32E          ST7789P3 显示屏
--------          --------------
3.3V      ----->  VCC
GND       ----->  GND
GPIO 18   ----->  SCL/SCK (SPI时钟)
GPIO 23   ----->  SDA/MOSI (SPI数据)
GPIO 19   ----->  MISO (SPI数据输入,可选)
GPIO 5    ----->  CS (片选)
GPIO 16   ----->  DC/RS (数据/命令)
GPIO 17   ----->  RST (复位)
```

## 所需库

### 1. NimBLE-Arduino
- **用途**: 低功耗蓝牙通信 (替代默认的 BLE 库，更省内存)
- **安装方式**: Arduino IDE -> 工具 -> 管理库 -> 搜索 "NimBLE-Arduino"
- **作者**: h2zero
- **版本**: 1.4.0+

### 2. TFT_eSPI
- **用途**: TFT 显示屏驱动
- **安装方式**: Arduino IDE -> 工具 -> 管理库 -> 搜索 "TFT_eSPI"
- **作者**: Bodmer
- **版本**: 2.5.0+

### TFT_eSPI 配置步骤

1. 安装 TFT_eSPI 库后，找到库文件夹:
   - Windows: `文档/Arduino/libraries/TFT_eSPI`
   - Android (ArduinoDroid): `/storage/emulated/0/Arduino/libraries/TFT_eSPI`

2. **重要**: 将本项目中的 `User_Setup.h` 复制到 TFT_eSPI 库文件夹中，覆盖默认配置。

3. 根据你的实际接线，修改 `User_Setup.h` 中的引脚定义:
   ```cpp
   #define TFT_CS   5    // 片选引脚
   #define TFT_DC   16   // 数据/命令引脚
   #define TFT_RST  17   // 复位引脚
   ```

4. 如果屏幕显示颜色异常，修改颜色顺序:
   ```cpp
   #define TFT_RGB_ORDER TFT_BGR   // 尝试 TFT_RGB 或 TFT_BGR
   ```

## 配置 BMS 信息

在代码顶部修改你的 BMS 信息:
```cpp
#define BMS_MAC_ADDRESS     "98:DA:20:07:B9:00"   // 你的BMS蓝牙MAC地址
#define BMS_DEVICE_NAME     "JK_BD4A24S10P"        // 你的BMS名称
```

## UI 布局说明

```
+----------------------------------+
| JK-BMS JK_BD4A24S10P        [●] |  <- 标题栏 + 连接状态
+----------------------------------+
|                                  |
|            POWER                 |  <- 功率标签
|          1,234 W                 |  <- 超大功率数字 (充电绿/放电红)
|         DISCHARGING              |  <- 充放电状态
|                                  |
+----------------------------------+
| VOLT     CURRENT      SOC        |
| 52.34V   23.56A      87%        |  <- 电压/电流/SOC
+----------------------------------+
| CAPACITY                         |
| 234.5 / 280.0 Ah                 |  <- 容量数值
| [████████████░░░░░░░░]           |  <- 进度条
+----------------------------------+
| TEMP                             |
| T1:25.5C  T2:26.1C  MOS:32.3C  |  <- 温度信息
+----------------------------------+
| [CHG] [DIS] Update: 2s ago      |  <- 状态栏
+----------------------------------+
```

## 编译上传

1. 在 ArduinoDroid 中:
   - 开发板选择: **ESP32 Dev Module**
   - 分区方案: **Default 4MB with spiffs**
   - 上传速度: **921600**

2. 点击上传，等待编译完成

3. 打开串口监视器 (115200波特率) 查看调试信息

## 故障排查

### 屏幕不显示
- 检查 SPI 引脚接线
- 确认 TFT_eSPI 的 User_Setup.h 已正确配置
- 尝试调整 SPI 频率: `#define SPI_FREQUENCY 20000000`

### BLE 连接失败
- 确认 BMS 已上电且蓝牙开启
- 检查 MAC 地址是否正确
- 使用 nRF Connect APP 扫描确认 BMS 可见

### 数据显示异常
- 确认 BMS 协议版本 (JK02_32S)
- 检查串口输出中的原始数据帧
- 不同版本 BMS 的数据偏移可能不同

## 协议参考

本项目基于 [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) 的协议文档实现。

### BLE 服务
- Service UUID: `0xFFE0`
- Characteristic UUID: `0xFFE1`

### 命令帧格式
```
AA 55 90 EB [CMD] [LEN] [VALUE x4] [PADDING x9] [CRC]
```

### 数据帧解析 (JK02_32S)
| 数据项 | 字节位置 | 系数 | 单位 |
|--------|---------|------|------|
| 电池电压 | 118-121 | 0.001 | V |
| 电池功率 | 122-125 | 0.001 | W |
| 电流 | 126-129 | 0.001 | A |
| 温度1 | 130-131 | 0.1 | °C |
| 温度2 | 132-133 | 0.1 | °C |
| SOC | 141 | 1 | % |
| 剩余容量 | 142-145 | 0.001 | Ah |
| 总容量 | 146-149 | 0.001 | Ah |

## 开源协议

本项目协议解析部分参考了以下开源项目:
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) - ESPHome JK-BMS 组件
- [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms) - Arduino JK-BMS 监控
