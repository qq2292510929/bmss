# JK-BMS 蓝牙监控显示器

## 项目概述
使用 ESP32-32E 通过蓝牙连接 JK-BMS (JK_BD4A24S10P)，在 2.8寸 ST7789 显示屏上实时显示电池状态。

## 硬件清单
- **开发板**: ESP32-32E (DevKitC)
- **显示屏**: 2.8寸 ST7789 IPS LCD (320x240)
- **BMS**: JK_BD4A24S10P (协议版本: JK02_32S)
- **BMS蓝牙MAC**: `98:da:20:07:b9:00`

## 接线图

### ESP32-32E ↔ ST7789 显示屏
```
ESP32          ST7789
------------------------
3.3V    →    VCC
GND     →    GND
GPIO18  →    SCK (SCL)
GPIO23  →    MOSI (SDA)
GPIO19  →    MISO (可选)
GPIO5   →    CS
GPIO16  →    DC (RS)
GPIO17  →    RST (RES)
GPIO4   →    BL (背光)
```

### 注意
- 所有信号线使用 3.3V 逻辑电平
- 背光引脚(BL)可以接 PWM 实现亮度调节，这里直接接高电平常亮

## 依赖库安装

在 Arduino IDE 中安装以下库：

1. **NimBLE-Arduino** (by h2zero)
   - 搜索: `NimBLE-Arduino`
   - 版本: 1.4.0+
   - 说明: 轻量级BLE库，比官方BLE库节省内存

2. **TFT_eSPI** (by Bodmer)
   - 搜索: `TFT_eSPI`
   - 版本: 2.5.0+
   - 说明: 高性能TFT驱动库

3. **ArduinoJson** (可选)
   - 搜索: `ArduinoJson`
   - 版本: 6.x

## TFT_eSPI 配置

**重要**: 必须正确配置 TFT_eSPI 库才能驱动 ST7789。

### 方法: 修改 User_Setup.h

1. 找到 Arduino 库文件夹中的 `TFT_eSPI`
   - Windows: `文档/Arduino/libraries/TFT_eSPI`
   - Linux: `~/Arduino/libraries/TFT_eSPI`

2. 备份并替换 `User_Setup.h` 文件，使用项目中提供的 `User_Setup.h`

3. 或者手动编辑 `User_Setup.h`，确保以下定义：
```cpp
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17
#define SPI_FREQUENCY  40000000
```

## 上传代码

1. 在 Arduino IDE 中打开 `JK_BMS_Display.ino`
2. 选择开发板: `Tools` → `Board` → `ESP32 Arduino` → `ESP32 Dev Module`
3. 选择端口: `Tools` → `Port` → 你的ESP32串口
4. 点击上传按钮

## UI布局说明 (横屏 320x240)

```
+------------------------------------------+
| JK-BMS Monitor              [BT] ONLINE  |  <- 标题栏 + 蓝牙状态
+------------------------------------------+
|                                          |
|         REAL-TIME POWER                  |  <- 主功率卡片 (大)
|              1250 W                      |
|              DISCHARGING                 |
|                                          |
+------------------------------------------+
|  VOLTAGE          |          CURRENT     |  <- 电压/电流条
|  52.50V           |          23.80A      |
|        [↓]                               |  <- 电流方向箭头
+------------------------------------------+
|  CAPACITY         |  TEMPERATURE         |  <- 容量 + 温度卡片
|  85%              |  28.5 C              |
|  85.0/100Ah       |  MOS:32C             |
|  [========]       |  [=======]           |  <- 进度条
+------------------------------------------+
```

## 显示内容

| 区域 | 内容 | 说明 |
|------|------|------|
| 主功率卡片 | 实时功率 (W/kW) | 最醒目显示，一眼看懂电池出力 |
| 电压/电流条 | 总电压 + 电流 | 带充放电方向箭头 |
| 容量卡片 | SOC% + 已用/总容量 | 绿色进度条 |
| 温度卡片 | 电池温度 + MOS温度 | 橙色温度条 |
| 右上角 | 蓝牙连接状态 | 绿色=在线, 红色=离线 |

## 数据解析

代码实现了完整的 **JK02_32S 协议解析**：

- **帧类型 0x02**: 电池实时数据 (电压、电流、功率、温度、SOC等)
- **帧类型 0x03**: 设备信息 (型号、固件版本等)
- **帧类型 0x01**: BMS设置参数

## 故障排查

### 显示屏不亮
- 检查背光引脚是否连接正确
- 确认 `TFT_eSPI` 的 `User_Setup.h` 已正确配置
- 检查 SPI 接线是否牢固

### 蓝牙连接失败
- 确认 BMS 已开启蓝牙广播
- 检查 MAC 地址是否正确
- 确保 ESP32 与 BMS 距离在 10 米内
- 查看串口监视器 (115200波特率) 获取调试信息

### 数据显示异常
- 检查 CRC 校验是否通过
- 确认 BMS 协议版本为 JK02_32S
- 查看串口输出确认数据帧解析情况

## 串口调试

打开串口监视器 (115200 波特率)，可以看到：
```
JK-BMS Monitor Starting...
Scanning for BMS...
Device found: JK_BD4A24S10P [98:da:20:07:b9:00]
Target BMS found!
Connecting to BMS...Connected to BLE server
Notifications registered
Sent command: 0x97
Sent command: 0x96
BMS ready - waiting for data...
[BMS] V:52.50V I:23.80A P:1249.5W SOC:85% T1:28.5C T2:29.0C MOS:32.0C
```

## 开源协议参考

本项目参考了以下开源项目的协议文档：
- [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) - JK-BMS协议文档
- [peff74/Arduino-jk-bms](https://github.com/peff74/Arduino-jk-bms) - Arduino NimBLE实现

## 许可证

MIT License
