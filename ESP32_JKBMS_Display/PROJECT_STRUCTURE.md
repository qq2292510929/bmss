# 项目结构说明

```
ESP32_JKBMS_Display/
├── src/                          # 源代码目录
│   ├── main.cpp                  # 主程序入口
│   ├── JKBMS_Protocol.h/.cpp     # JK BMS协议解析
│   ├── JKBMS_Bluetooth.h/.cpp    # 蓝牙连接管理
│   ├── Battle_UI.h/.cpp          # 战斗风格UI
│   ├── Config.h                  # 配置文件
│   ├── Logger.h/.cpp             # 日志工具
│   ├── DebugUtils.h/.cpp         # 调试工具
│   └── ExampleConfig.h           # 配置示例
├── lib/                          # 第三方库
│   └── TFT_eSPI/                 # 屏幕驱动库
│       ├── User_Setup_Select.h   # 用户配置
│       ├── User_Config.h         # 用户配置
│       └── Setup.h               # 默认配置
├── test/                         # 测试目录
├── docs/                         # 文档目录
├── platformio.ini                # PlatformIO配置
├── library.json                  # 库描述文件
├── README.md                     # 项目说明
├── QUICK_START.md               # 快速开始
├── build.sh                      # Linux构建脚本
└── build.bat                     # Windows构建脚本

```

## 核心模块说明

### 1. JKBMS_Protocol
负责解析JK02_32S协议的数据帧
- 帧头识别: 0x4A 0x4B 0xFF 0x5A
- CRC校验
- 数据提取和单位转换

### 2. JKBMS_Bluetooth
管理蓝牙连接和通信
- BLE设备扫描
- 连接管理
- 数据接收回调

### 3. Battle_UI
战斗风格用户界面
- 动态配色
- 圆角卡片
- 动画效果
- 实时数据展示

### 4. Config
配置管理
- 蓝牙参数
- 显示参数
- UI配置
- 调试选项

### 5. Logger
日志系统
- 分级日志
- 时间戳
- 串口输出

### 6. DebugUtils
调试工具
- 数据打印
- HEX转储
- 性能统计
- 调试覆盖层

## 硬件连接

ST7789 2.8" SPI屏幕:
- VCC: 3.3V
- GND: GND
- CS: GPIO15
- DC: GPIO2
- RST: GPIO4
- MOSI: GPIO23
- SCLK: GPIO18
- BL: GPIO21 (可选)

蓝牙: 使用ESP32内置BLE，无需额外接线

## 编译和上传

使用PlatformIO:
```bash
pio run --target upload
pio device monitor
```

使用Arduino IDE:
1. 安装TFT_eSPI和NimBLE库
2. 配置User_Setup_Select.h
3. 编译并上传

## 故障排查

1. 蓝牙连接失败
   - 检查设备名称和MAC地址
   - 确保BMS蓝牙已开启
   - 缩短距离避免干扰

2. 屏幕不显示
   - 检查SPI接线
   - 确认引脚配置
   - 检查屏幕供电

3. 数据不更新
   - 确认协议版本
   - 检查串口波特率
   - 查看错误日志

## 自定义配置

修改 `src/Config.h` 中的配置来定制:
- 蓝牙设备参数
- 显示刷新率
- 最大功率阈值
- UI动画速度

## 性能优化

- 使用双缓冲减少闪烁
- 限制UI刷新频率
- 优化圆角绘制算法
- 使用PSRAM扩展内存

## 扩展功能

计划添加:
- WiFi远程监控
- 数据记录和存储
- OTA无线更新
- 触摸屏控制
- 多语言界面
- 告警通知
