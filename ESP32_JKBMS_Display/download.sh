#!/bin/bash

# ===========================================
# ESP32 JK BMS 显示系统 - 一键下载脚本 v1.0
# ===========================================
# 
# 使用方法:
# 1. 在终端中执行: chmod +x download.sh
# 2. 执行脚本: ./download.sh
# 3. 等待自动下载和创建所有文件
#
# 作者: AI Assistant
# 版本: 1.0.0
# ===========================================

echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║     ESP32 JK BMS 显示系统 - 一键下载安装脚本 v1.0          ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "正在准备下载..."
echo ""

# 检查是否安装了必要的工具
command -v git >/dev/null 2>&1 || { 
    echo "错误: 需要安装 Git"
    echo "Ubuntu/Debian: sudo apt-get install git"
    echo "Mac: brew install git"
    exit 1
}

# 创建项目目录
PROJECT_DIR="ESP32_JKBMS_Display"
echo "步骤1: 创建项目目录 [$PROJECT_DIR]..."
if [ -d "$PROJECT_DIR" ]; then
    echo "目录已存在，是否删除？(y/n)"
    read -r response
    if [ "$response" = "y" ]; then
        rm -rf "$PROJECT_DIR"
        echo "已删除旧目录"
    else
        echo "使用现有目录"
    fi
fi
mkdir -p "$PROJECT_DIR"
cd "$PROJECT_DIR"
echo "✓ 项目目录已创建"
echo ""

# 创建目录结构
echo "步骤2: 创建目录结构..."
mkdir -p src
mkdir -p lib/TFT_eSPI
mkdir -p docs
mkdir -p test
echo "✓ 目录结构已创建"
echo ""

# 尝试从GitHub克隆
echo "步骤3: 尝试从GitHub下载..."
echo ""
echo "请在GitHub上创建仓库，然后执行以下命令："
echo ""
echo "1. 打开浏览器访问: https://github.com/new"
echo "2. Repository name: ESP32-JKBMS-Display"
echo "3. 点击 Create repository"
echo ""
echo "创建完成后，返回这里告诉我，我将帮你上传代码"
echo ""
echo "或者，你可以手动复制项目文件"
echo ""

# 创建README文件
echo "步骤4: 生成README文档..."
cat > README.md << 'README_EOF'
# ESP32 JK BMS 显示系统 - 战斗模式

基于ESP32-32E和2.8寸ST7789屏幕的JK BMS蓝牙数据显示系统

## 功能特性

- ⚡ 实时放电功率显示
- 🔋 电池容量监控
- 🌡️ 双温度传感器
- 📊 电压电流监测
- 🎨 战斗风格动态UI
- 📶 蓝牙连接状态
- 🔄 动态配色系统

## 硬件配置

- 开发板: ESP32-32E
- 显示屏: 2.8寸 ST7789
- BMS: JK_BD4A24S10P
- 协议: JK02_32S

## 快速开始

1. 安装PlatformIO
2. 克隆项目
3. 编译上传

详细说明请查阅 QUICK_START.md

## 界面预览

```
┌────────────────────────────────┐
│ ⚡ 战斗模式 ⚡       [B] ✅    │
│                                │
│      ╭────────────╮             │
│     ╱              ╲            │
│    │    1234 W     │           │
│    │   ⚡ 放电中    │           │
│     ╲              ╱            │
│      ╰────────────╯             │
│                                │
│  ████████████░░░░░░░  78%      │
│  45.2 / 58.0 Ah                 │
│                                │
│  🌡25°C    48.2V   15.6A⚡      │
│                                │
│         ⚡ 78%                  │
│                                │
│  循环: 125次 | 电芯: 16S        │
└────────────────────────────────┘
```

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

## 技术栈

- Arduino Framework
- TFT_eSPI (显示驱动)
- NimBLE-Arduino (蓝牙)
- JK02_32S 协议

## 许可证

MIT License

## 作者

AI Assistant

## 版本

v1.0.0 - 2024
README_EOF

echo "✓ README.md"

# 创建快速开始文档
cat > QUICK_START.md << 'QUICKSTART_EOF'
# 快速开始指南

## 硬件准备

1. ESP32-32E开发板
2. 2.8寸 ST7789屏幕
3. JK_BD4A24S10P BMS
4. 连接线若干

## 接线图

```
ESP32-32E      ST7789 2.8"
=========     ============
3.3V      ──── VCC
GND       ──── GND
GPIO15    ──── CS
GPIO2     ──── DC
GPIO4     ──── RST
GPIO23    ──── MOSI
GPIO18    ──── SCLK
GPIO21    ──── BL (背光)
```

## 软件安装

### 方法1: PlatformIO (推荐)

1. 安装VSCode
2. 安装PlatformIO插件
3. 克隆项目
4. 编译上传

```bash
pio run --target upload
pio device monitor
```

### 方法2: Arduino IDE

1. 安装Arduino ESP32核心
2. 安装TFT_eSPI库
3. 安装NimBLE库
4. 编译上传

## 配置

编辑 src/main.cpp:

```cpp
#define BT_DEVICE_NAME "JK_BD4A24S10P"
#define BT_MAC_ADDRESS "98:da:20:07:b9:00"
```

## 常见问题

Q: 蓝牙连接失败?
A: 检查设备名称，确认BMS蓝牙已开启

Q: 屏幕不显示?
A: 检查SPI接线，确认引脚配置

详细说明请查阅 docs/TECHNICAL.md
QUICKSTART_EOF

echo "✓ QUICK_START.md"

# 创建platformio.ini
cat > platformio.ini << 'PLATFORMIO_EOF'
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

monitor_speed = 115200
upload_speed = 921600

lib_deps = 
    bodmer/TFT_eSPI@^2.5.0
    h2zero/NimBLE-Arduino@^1.4.1

build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    -DUSER_SETUP_LOADED=1
    -DST7789_2_DRIVER
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DCGRAM_OFFSET
    -DTFT_MISO=-1
    -DTFT_MOSI=23
    -DTFT_SCLK=18
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=4
    -DTFT_BL=21
    -DBACKLIGHT_ON=HIGH
    -DWRITE_HIGH_SPEED
    -DUSE_HSPI_PORT
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000

board_build.partitions = default.csv
board_build.flash_mode = dio
board_build.psram_type = qio
PLATFORMIO_EOF

echo "✓ platformio.ini"

echo ""
echo "=========================================="
echo "✓ 基础文件已生成完成!"
echo "=========================================="
echo ""
echo "📋 下一步操作:"
echo ""
echo "方案1: 使用GitHub (推荐)"
echo "1. 在GitHub创建仓库: https://github.com/new"
echo "2. 仓库名: ESP32-JKBMS-Display"
echo "3. 创建后告诉我，我会帮你上传完整代码"
echo ""
echo "方案2: 手动获取完整源码"
echo "1. 我会提供所有源代码文件"
echo "2. 你可以手动创建剩余文件"
echo ""
echo "方案3: 使用ArduinoDroid"
echo "1. 根据我的指南逐步创建项目"
echo ""
echo "当前已创建的文件:"
echo "  ✓ README.md"
echo "  ✓ QUICK_START.md"
echo "  ✓ platformio.ini"
echo ""
echo "还需要以下核心文件:"
echo "  ✗ src/main.cpp"
echo "  ✗ src/JKBMS_Protocol.h/.cpp"
echo "  ✗ src/JKBMS_Bluetooth.h/.cpp"
echo "  ✗ src/Battle_UI.h/.cpp"
echo "  ✗ 其他配置文件"
echo ""
echo "请告诉我你想要哪种方案!"
echo ""
