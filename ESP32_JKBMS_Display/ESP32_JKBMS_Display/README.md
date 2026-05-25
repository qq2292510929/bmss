# ESP32 JK BMS 显示系统 - 战斗模式

基于ESP32-32E和2.8寸ST7789屏幕的JK BMS蓝牙数据显示系统

## 功能特性

- 实时放电功率显示（大号数字，颜色随功率变化）
- 电池容量监控（进度条 + 百分比）
- 双温度传感器显示
- 电压电流监测
- 蓝牙连接状态（右上角指示）
- 战斗风格动态UI
- 全中文界面

## 硬件配置

- 开发板: ESP32-32E
- 显示屏: 2.8寸 ST7789
- BMS: JK_BD4A24S10P
- 协议: JK02_32S
- 蓝牙MAC: 98:da:20:07:b9:00

## 硬件接线

```
ESP32-32E      ST7789 2.8"
3.3V      ──── VCC
GND       ──── GND
GPIO15    ──── CS
GPIO2     ──── DC
GPIO4     ──── RST
GPIO23    ──── MOSI
GPIO18    ──── SCLK
GPIO21    ──── BL
```

## 快速开始

### 1. 安装PlatformIO

```bash
# 安装VSCode
# 安装PlatformIO插件

# 或使用命令行
pip install platformio
```

### 2. 编译上传

```bash
cd ESP32_JKBMS_Display
pio run
pio run --target upload
pio device monitor
```

## 界面预览

```
BATTLE MODE              [B] ONLINE

     ╭────────────╮
    ╱              ╲
   │    1234 W     │
   │  DISCHARGING   │
    ╲              ╱
     ╰────────────╯

CAPACITY
████████████████░░░░░░  78%
45.2 / 58.0 Ah

TEMPERATURE          VOLTAGE
25C              48.2V

SOC: 78%

Cycles: 125 | Cells: 16S
```

## 战斗模式配色

- 低功率(0-1kW): 绿色（节能模式）
- 中功率(1-2.5kW): 黄色（普通模式）
- 高功率(2.5-4kW): 橙色（运动模式）
- 满功率(4kW+): 红色（战斗模式）

## 技术栈

- Arduino Framework
- TFT_eSPI (显示驱动)
- NimBLE-Arduino (蓝牙)
- JK02_32S 协议

## 许可证

MIT License

## 版本

v1.0.0 - 2024
