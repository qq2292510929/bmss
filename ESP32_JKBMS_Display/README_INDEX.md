# 📚 ESP32 JK BMS 显示系统 - 文档索引

欢迎使用ESP32 JK BMS显示系统！本项目提供完整的文档支持。

---

## 🚀 快速开始

### 新用户必读
1. **[快速开始指南](QUICK_START.md)** - 5分钟快速上手
   - 硬件准备
   - 软件安装
   - 接线说明
   - 编译上传

2. **[项目总结](PROJECT_SUMMARY.md)** - 完整项目概览
   - 已完成功能
   - 技术亮点
   - 使用说明

---

## 📖 核心文档

### 用户文档
- **[README主文档](README.md)** - 项目完整说明
  - 功能特性
  - 硬件配置
  - 安装步骤
  - 使用说明
  - 故障排除

### 技术文档
- **[技术详解](docs/TECHNICAL.md)** - 深入技术分析
  - 系统架构
  - 蓝牙协议
  - 显示系统
  - 性能优化
  - 调试指南

### 项目结构
- **[结构说明](PROJECT_STRUCTURE.md)** - 代码组织
  - 目录结构
  - 模块说明
  - 配置指南

### 界面演示
- **[UI效果说明](docs/UI_DEMO.md)** - 界面详细说明
  - 布局结构
  - 动态效果
  - 颜色系统
  - 动画描述

---

## 🔧 开发指南

### 环境配置
```
推荐工具: PlatformIO (VSCode插件)
替代工具: Arduino IDE
```

### 编译和上传
```bash
# 使用PlatformIO
cd ESP32_JKBMS_Display
pio run --target upload
pio device monitor

# 或使用构建脚本
./build.sh upload      # Linux/Mac
build.bat upload       # Windows
```

### 配置修改
主要配置文件: `src/main.cpp`
```cpp
#define BT_DEVICE_NAME "JK_BD4A24S10P"
#define BT_MAC_ADDRESS "98:da:20:07:b9:00"
```

---

## 🎯 功能特性

### ✅ 已实现功能
| 功能 | 说明 | 文档 |
|------|------|------|
| 蓝牙连接 | 自动扫描连接BMS | [JKBMS_Bluetooth.h](src/JKBMS_Bluetooth.h) |
| 协议解析 | JK02_32S完整支持 | [JKBMS_Protocol.h](src/JKBMS_Protocol.h) |
| 战斗UI | 全中文动态界面 | [Battle_UI.h](src/Battle_UI.h) |
| 动态配色 | 功率变色效果 | [Battle_UI.cpp](src/Battle_UI.cpp) |
| 圆角卡片 | 美观界面设计 | [Battle_UI.cpp](src/Battle_UI.cpp) |

### 🎨 UI特色
- ⚡ **实时功率** - 主视觉中心
- 🔋 **容量显示** - 进度条+百分比
- 🌡️ **温度监测** - 双温度传感器
- 📊 **电压电流** - 实时数据
- 📶 **蓝牙状态** - 右上角指示
- 🎨 **战斗风格** - 运动模式配色

---

## 🛠️ 故障排查

### 常见问题

#### 1. 蓝牙连接失败
**症状**: 显示"未连接"
**解决方案**:
1. 检查BMS蓝牙是否开启
2. 确认设备名称: `JK_BD4A24S10P`
3. 缩短距离避免干扰
4. 重启BMS和ESP32
**参考**: [README.md - 故障排除](README.md#故障排除)

#### 2. 屏幕不显示
**症状**: 黑屏或无响应
**解决方案**:
1. 检查SPI接线 (CS, DC, RST, MOSI, SCLK)
2. 确认3.3V供电
3. 检查引脚配置
4. 尝试调整屏幕旋转
**参考**: [QUICK_START.md](QUICK_START.md#常见问题)

#### 3. 数据不更新
**症状**: 数值保持不变
**解决方案**:
1. 检查串口波特率 (115200)
2. 确认协议版本 (JK02_32S)
3. 查看串口错误信息
4. 重启系统
**参考**: [docs/TECHNICAL.md - 调试指南](docs/TECHNICAL.md#8-调试指南)

---

## 📦 项目文件

### 源代码结构
```
src/
├── main.cpp              # 主程序入口 ⭐
├── JKBMS_Protocol.*      # 协议解析核心
├── JKBMS_Bluetooth.*    # 蓝牙连接管理
├── Battle_UI.*           # UI绘制引擎
├── Config.h             # 系统配置
├── Logger.*             # 日志工具
├── DebugUtils.*          # 调试工具
└── ExampleConfig.h      # 配置示例
```

### 配置文件
```
platformio.ini           # PlatformIO项目配置
lib/TFT_eSPI/*          # 屏幕驱动配置
src/Config.h            # 系统配置
```

### 构建脚本
```
build.sh                # Linux/Mac构建脚本
build.bat               # Windows构建脚本
install.sh              # 安装脚本
```

---

## 🔗 相关资源

### 硬件文档
- [ESP32技术手册](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ST7789数据手册](https://www.displayfuture.com/Display/datasheet/controller/ST7789.pdf)
- [JK BMS协议文档](https://github.com/jblance/jkbms) (第三方)

### 开发工具
- [PlatformIO官网](https://platformio.org/)
- [TFT_eSPI库](https://github.com/Bodmer/TFT_eSPI)
- [NimBLE库](https://github.com/h2zero/NimBLE-Arduino)

### 学习资源
- ESP32蓝牙编程
- TFT屏幕驱动开发
- 电池管理系统原理

---

## 📞 获取帮助

### 问题反馈
- GitHub Issues: 提交Bug或功能请求
- 详细描述问题现象
- 提供错误日志
- 说明硬件配置

### 社区支持
- Arduino中文社区
- ESP32技术交流群
- PlatformIO论坛

---

## 🗺️ 阅读路径

### 推荐学习顺序

#### 初级用户
1. 快速开始 → README → 故障排除
2. 编译上传 → 测试功能
3. 根据需要查阅其他文档

#### 中级用户
1. 项目总结 → 项目结构
2. 技术文档 → 核心模块
3. 修改配置 → 自定义功能

#### 高级用户
1. 技术文档 → 完整阅读
2. 源码分析 → 核心算法
3. 性能优化 → 扩展功能
4. 贡献代码 → Pull Request

---

## 📋 文档清单

| 文档 | 类型 | 用途 | 优先级 |
|------|------|------|--------|
| README.md | 必读 | 项目总览 | ⭐⭐⭐ |
| QUICK_START.md | 必读 | 快速上手 | ⭐⭐⭐ |
| PROJECT_SUMMARY.md | 推荐 | 功能总结 | ⭐⭐ |
| PROJECT_STRUCTURE.md | 推荐 | 代码结构 | ⭐⭐ |
| docs/TECHNICAL.md | 参考 | 技术细节 | ⭐⭐ |
| docs/UI_DEMO.md | 参考 | 界面说明 | ⭐⭐ |

---

## ✅ 检查清单

开始使用前请确认:
- [ ] 已阅读快速开始指南
- [ ] 准备好所有硬件
- [ ] 安装了开发环境
- [ ] 理解了接线图
- [ ] 知道如何编译上传
- [ ] 准备测试设备

---

**祝你使用愉快！有任何问题随时提问！** ⚡🔋

---

最后更新: 2024年
版本: 1.0.0
