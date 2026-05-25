# ESP32 JK BMS 显示系统

## 快速开始指南

### 所需硬件
1. ESP32-32E 开发板
2. 2.8寸 ST7789 TFT LCD (SPI接口)
3. JK_BD4A24S10P 锂电池管理系统
4. 连接线若干
5. USB数据线

### 接线图

```
ESP32-32E          ST7789 2.8"
=========         ============
3.3V      ────    VCC
GND       ────    GND
GPIO15    ────    CS (Chip Select)
GPIO2     ────    DC (Data Command)
GPIO4     ────    RST (Reset)
GPIO23    ────    MOSI
GPIO18    ────    SCLK
GPIO21    ────    BL (Backlight, 可选)
```

### 软件安装

#### 方法1: PlatformIO (推荐)

1. **安装PlatformIO**
   - 下载并安装 [VSCode](https://code.visualstudio.com/)
   - 在VSCode中安装 PlatformIO IDE 插件
   - 等待安装完成

2. **打开项目**
   - 打开VSCode
   - File → Open Folder
   - 选择 `ESP32_JKBMS_Display` 文件夹

3. **编译和上传**
   ```bash
   # 编译项目
   pio run
   
   # 上传到ESP32
   pio run --target upload
   
   # 打开串口监视器
   pio device monitor
   ```

#### 方法2: Arduino IDE

1. **安装Arduino ESP32核心**
   - 打开 Arduino IDE
   - File → Preferences
   - 在 Additional Boards Manager URLs 添加:
     ```
     https://dl.espressif.com/dl/package_esp32_index.json
     ```
   - Tools → Board → Boards Manager
   - 搜索 "ESP32" 并安装

2. **安装库**
   - Sketch → Include Library → Manage Libraries
   - 安装以下库:
     - TFT_eSPI by Bodmer
     - NimBLE-Arduino by h2zero

3. **配置库**
   - 复制 `lib/TFT_eSPI/User_Setup_Select.h` 到 Arduino libraries 目录

4. **上传**
   - 打开 `src/main.cpp`
   - Tools → Board → ESP32 Dev Module
   - 选择正确的端口
   - Upload

### 配置说明

编辑 `src/main.cpp` 文件中的蓝牙配置:

```cpp
#define BT_DEVICE_NAME "JK_BD4A24S10P"    // 你的BMS名称
#define BT_MAC_ADDRESS "98:da:20:07:b9:00" // 你的BMS MAC地址
```

### 常见问题

#### Q: 蓝牙连接不上?
A: 
- 确保BMS蓝牙已开启
- 检查设备名称是否正确
- 尝试缩短距离
- 重启BMS和ESP32

#### Q: 屏幕不显示?
A:
- 检查所有接线
- 确认SPI引脚配置正确
- 检查屏幕电压(3.3V)
- 尝试调整屏幕旋转角度

#### Q: 数据不更新?
A:
- 检查串口波特率(115200)
- 确认协议版本
- 查看串口监视器的错误信息

### 功能说明

#### 主界面布局

```
┌─────────────────────────────────┐
│      ⚡ 战斗模式 ⚡    [B] ✅   │  ← 标题 + 蓝牙状态
├─────────────────────────────────┤
│                                 │
│        ╭─────────────╮         │
│        │             │         │
│        │    1234 W   │         │  ← 实时功率 (大字)
│        │   ⚡放电中   │         │
│        ╰─────────────╯         │
│                                 │
│  ┌─ 电池容量 ─────────────────┐ │
│  │ ████████████░░░░░  78%     │ │  ← 容量进度条
│  │ 45.2 / 58.0 Ah             │ │
│  └────────────────────────────┘ │
│                                 │
│  ┌─ 温度 ────┐ ┌─ 电压 ────┐   │
│  │ 🌡 25°C    │ │ 48.2V     │   │  ← 温度和电压
│  │     26°C   │ │ 15.6A ⚡   │   │
│  └────────────┘ └────────────┘   │
│                                 │
│  ┌─ ⚡ 78% ───────────────────┐ │
│  └────────────────────────────┘ │  ← SOC
│                                 │
│  循环: 125次 | 电芯: 16S        │  ← 页脚信息
└─────────────────────────────────┘
```

#### 动态效果

- **功率环**: 实时反映放电功率大小
- **颜色变化**: 功率越高颜色越激烈
- **脉冲动画**: 充放电状态指示
- **边框呼吸**: 高功率时的特效

### 技术支持

如遇到问题:
1. 查看串口输出的错误信息
2. 检查硬件连接
3. 确认配置文件
4. 提交Issue到GitHub

### 许可证

MIT License - 自由使用和修改

### 贡献指南

欢迎提交Pull Request和Issue!

---

**享受你的战斗模式BMS显示! ⚡**
