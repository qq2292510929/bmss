# ESP32 JK BMS 显示系统 - 一键安装指南

## 🎯 快速开始 (3分钟)

### 步骤1: 获取脚本文件

你有两个选择:

#### 选项A: 在GitHub上创建仓库 (推荐)
1. 访问 https://github.com/new
2. 创建新仓库: `ESP32-JKBMS-Display`
3. 创建后告诉我，我会帮你上传所有代码
4. 然后在本地克隆:
```bash
git clone https://github.com/qq2292510929/ESP32-JKBMS-Display.git
cd ESP32-JKBMS-Display
```

#### 选项B: 直接下载脚本
1. 复制 `install.sh` 文件内容
2. 在本机创建文件: `install.sh`
3. 添加执行权限: `chmod +x install.sh`
4. 运行: `./install.sh`

---

## 🚀 一键安装脚本使用说明

### 自动安装步骤

```bash
# 1. 给脚本添加执行权限
chmod +x install.sh

# 2. 运行安装脚本
./install.sh

# 3. 进入项目目录
cd ESP32_JKBMS_Display

# 4. 编译项目
pio run

# 5. 上传到ESP32
pio run --target upload

# 6. 打开串口监视器 (可选)
pio device monitor
```

脚本会自动创建以下文件:
- ✅ platformio.ini
- ✅ src/main.cpp
- ✅ src/JKBMS_Protocol.h
- ✅ src/JKBMS_Protocol.cpp
- ✅ src/JKBMS_Bluetooth.h
- ✅ src/JKBMS_Bluetooth.cpp
- ✅ src/Battle_UI.h
- ✅ src/Battle_UI.cpp

---

## 📋 硬件准备

在运行脚本前，请确保准备好以下硬件:

1. **ESP32-32E 开发板** × 1
2. **2.8寸 ST7789 屏幕** × 1
3. **杜邦线** 若干
4. **USB数据线** × 1

---

## 🔌 接线图

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
GPIO21    ──── BL
```

---

## 💻 软件要求

### 安装PlatformIO

#### Windows
1. 下载VSCode: https://code.visualstudio.com/
2. 安装VSCode
3. 打开VSCode，安装PlatformIO插件
4. 重启VSCode

#### Linux/Mac
```bash
# 安装VSCode
# 下载: https://code.visualstudio.com/

# 或者使用命令行安装PlatformIO
pip install platformio
```

---

## 🔧 编译和上传

```bash
# 进入项目目录
cd ESP32_JKBMS_Display

# 编译项目
pio run

# 上传到ESP32
pio run --target upload

# 查看串口输出
pio device monitor
```

---

## ✅ 验证安装

成功标志:
1. 屏幕显示 "BATTLE MODE"
2. 右上角显示蓝牙状态
3. 串口显示 "Display initialized!"
4. 蓝牙连接后显示实时数据

---

## 🎨 功能特性

安装成功后，你将看到:

- ⚡ **实时功率显示** - 大号数字，颜色随功率变化
- 🔋 **容量监控** - 进度条 + 百分比
- 🌡️ **温度显示** - 双温度传感器
- 📶 **蓝牙状态** - 右上角指示
- 🎨 **战斗模式** - 动态配色和动画
- 🔄 **全中文界面** - 所有信息中文显示

---

## 🐛 故障排查

### 问题1: 编译失败
```
解决方法:
1. 检查PlatformIO是否正确安装
2. 更新PlatformIO: pio upgrade
3. 安装依赖: pio lib install
```

### 问题2: 上传失败
```
解决方法:
1. 检查USB线是否支持数据传输
2. 选择正确的端口: pio device list
3. 按住BOOT按钮再点击上传
```

### 问题3: 屏幕不显示
```
解决方法:
1. 检查接线
2. 确认屏幕型号是ST7789
3. 检查屏幕供电 (3.3V)
```

### 问题4: 蓝牙连接失败
```
解决方法:
1. 确认BMS蓝牙已开启
2. 检查设备名称: JK_BD4A24S10P
3. 缩短距离
4. 重启BMS和ESP32
```

---

## 📚 文档资源

- README.md - 项目说明
- QUICK_START.md - 快速开始
- docs/TECHNICAL.md - 技术文档

---

## 🎉 成功！

恭喜你完成安装！享受你的战斗模式BMS显示吧！

如果遇到任何问题，请随时告诉我！

---

## 📞 获取帮助

遇到问题?
1. 查看故障排查章节
2. 检查接线
3. 查看串口错误信息
4. 告诉我具体的错误信息

---

**祝你使用愉快！⚡🔋**
