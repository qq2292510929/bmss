# ESP32 JK BMS 显示系统 - 技术文档

## 目录
1. 系统概述
2. 硬件架构
3. 软件架构
4. 蓝牙协议详解
5. 显示系统
6. UI设计规范
7. 性能优化
8. 调试指南

---

## 1. 系统概述

### 1.1 项目目标
创建一个基于ESP32-32E的实时电池管理系统显示终端，通过蓝牙连接JK_BD4A24S10P BMS，实时显示电池状态信息，并采用战斗风格的UI设计。

### 1.2 核心功能
- 实时功率显示
- 电池容量监控
- 温度监测
- 电压电流监控
- 蓝牙连接状态
- 动态配色效果
- 战斗模式动画

### 1.3 技术栈
- **硬件**: ESP32-32E, ST7789 2.8"
- **固件**: Arduino Framework
- **蓝牙**: NimBLE (BLE 4.2)
- **显示**: TFT_eSPI
- **协议**: JK02_32S

---

## 2. 硬件架构

### 2.1 ESP32-32E特性
- 双核Xtensa LX6处理器
- 520KB SRAM
- 4MB Flash
- WiFi/BT/BLE
- 34个GPIO引脚
- 3.3V工作电压

### 2.2 ST7789显示屏规格
- 尺寸: 2.8英寸
- 分辨率: 240x320
- 颜色深度: 16位 (65K色)
- 接口: SPI (4线)
- 工作电压: 3.3V
- 驱动IC: ST7789P3

### 2.3 引脚配置
```
电源:
- VIN: 5V-12V (外部电源)
- 3.3V: 屏幕和传感器供电
- GND: 公共地

SPI接口:
- GPIO23 (MOSI): 屏幕数据
- GPIO18 (SCLK): 屏幕时钟
- GPIO19 (MISO): 触摸屏数据(未用)

显示控制:
- GPIO15 (CS): 屏幕片选
- GPIO2 (DC): 数据/命令选择
- GPIO4 (RST): 屏幕复位
- GPIO21 (BL): 背光控制

蓝牙:
- 内置天线
- 无需额外接线
```

---

## 3. 软件架构

### 3.1 程序流程
```
setup()
├── 初始化串口 (115200)
├── 初始化TFT屏幕
├── 初始化蓝牙
├── 连接BMS
└── 进入主循环

loop()
├── 读取BMS数据
├── 更新显示
├── 检查连接状态
└── 延迟10ms
```

### 3.2 模块结构
```
┌─────────────────────────────────────┐
│           Main Loop                │
│    (100ms刷新周期)                   │
└─────────────┬───────────────────────┘
              │
    ┌─────────┴──────────┐
    │                    │
┌───▼─────────┐  ┌──────▼───────┐
│  Bluetooth  │  │  Display UI  │
│  Manager     │  │  Renderer    │
└──────────────┘  └──────────────┘
```

### 3.3 关键类

#### JKBMSProtocol
- `parseFrame()` - 解析接收到的数据帧
- `getData()` - 获取解析后的数据
- `isDataValid()` - 检查数据有效性

#### JKBMSBluetooth
- `connectToBMS()` - 连接到BMS设备
- `disconnect()` - 断开连接
- `isConnected()` - 检查连接状态
- `hasNewData()` - 检查是否有新数据

#### BattleUI
- `update()` - 更新整个UI
- `drawPowerGauge()` - 绘制功率表
- `drawCapacity()` - 绘制容量显示
- `drawTemperatures()` - 绘制温度
- `getPowerColor()` - 获取功率对应颜色

---

## 4. 蓝牙协议详解

### 4.1 JK02协议格式
```
偏移  长度  描述
0     4    帧头: 4A 4B FF 5A
4     2    数据长度 (大端序)
6     1    帧类型
7     10   产品信息
17    2    总电压 (×100, 大端序)
19    2    电流 (×100, 有符号, 大端序)
21    2    剩余容量 (×100, 大端序)
23    2    总容量 (×100, 大端序)
25    2    循环次数 (大端序)
27    2    充电状态
29    2    放电状态
31    1    电芯数量
32    1    温度传感器数量
33    2×N  温度值 (×10-2731, 大端序)
...   ...
47    2×N  电芯电压 (×1000, 大端序)
...   ...
N     2    CRC校验 (和校验)
```

### 4.2 帧类型
- 0x03: 实时数据帧 (主要使用)
- 0x15: 设备信息帧
- 0x16: 设置帧

### 4.3 CRC校验
采用简单求和校验:
```cpp
uint16_t calculateCRC(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}
```

### 4.4 连接流程
1. 初始化BLE设备
2. 开始扫描指定设备名
3. 发现目标设备后停止扫描
4. 建立GATT连接
5. 发现服务和特征值
6. 订阅通知
7. 接收数据帧

---

## 5. 显示系统

### 5.1 屏幕初始化
```cpp
tft.init();
tft.setRotation(1);  // 横屏模式
tft.fillScreen(TFT_BLACK);
```

### 5.2 字体设置
使用Noto Sans字体:
- NotoSansBold20: 功率数字
- NotoSansBold12: 标题和标签
- NotoSansBold10: 副标题
- NotoSansBold8: 正文
- NotoSansBold6: 小字说明

### 5.3 圆角绘制
```cpp
void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, 
                   int16_t r, uint16_t color) {
    // 绘制矩形主体
    fillRect(x + r, y, w - 2*r, h);
    fillRect(x, y + r, w, h - 2*r);
    
    // 绘制四个圆角
    fillCircleHelper(x + r, y + r, r, 1, h - 2*r);
    fillCircleHelper(x + w - r - 1, y + r, r, 2, h - 2*r);
}
```

### 5.4 颜色插值
```cpp
uint16_t interpolateColor(uint16_t c1, uint16_t c2, float t) {
    uint8_t r1 = (c1 >> 11) & 0x1F;
    uint8_t g1 = (c1 >> 5) & 0x3F;
    uint8_t b1 = c1 & 0x1F;
    
    uint8_t r2 = (c2 >> 11) & 0x1F;
    uint8_t g2 = (c2 >> 5) & 0x3F;
    uint8_t b2 = c2 & 0x1F;
    
    return color565(
        r1 + (r2 - r1) * t,
        g1 + (g2 - g1) * t,
        b1 + (b2 - b1) * t
    );
}
```

---

## 6. UI设计规范

### 6.1 布局结构
```
┌─────────────────────────────────┐
│ Header (30px)                   │
│ 标题 + 蓝牙状态                  │
├─────────────────────────────────┤
│ Power Gauge (130px)             │
│ 圆形功率仪表盘                   │
├─────────────────────────────────┤
│ Capacity Card (60px)            │
│ 容量进度条                       │
├─────────────────────────────────┤
│ Info Cards (70px)               │
│ 温度 | 电压电流                  │
├─────────────────────────────────┤
│ SOC Badge (35px)                │
│ 电量百分比                       │
├─────────────────────────────────┤
│ Footer (20px)                   │
│ 循环次数 | 电芯数                │
└─────────────────────────────────┘
```

### 6.2 颜色系统
```
主色调:
- 背景: #101828 (深蓝黑)
- 卡片: #18294A (深蓝)
- 边框: #505080 (灰蓝)

功率配色:
- 低功率: #0000FF → #00FF00 (蓝→绿)
- 中功率: #00FF00 → #FFFF00 (绿→黄)
- 高功率: #FFFF00 → #FF0000 (黄→红)
- 满功率: #FF0000 → #FFFF00 (红→白)

状态色:
- 充电: #00FF00 (绿)
- 放电: #FF0000 (红)
- 待机: #808080 (灰)
- 告警: #FFFF00 (黄)

文字色:
- 主文字: #FFFFFF (白)
- 副文字: #C0C0FF (浅蓝白)
- 强调: #FFD700 (金)
```

### 6.3 字体大小
```
功率数字:  32pt Bold
标题文字:  16pt Bold
正文:      12pt Bold
说明文字:   8pt Bold
最小字:     6pt Bold
```

### 6.4 间距规范
```
卡片间距: 5px
卡片内边距: 10px
元素间距: 5px
圆角半径: 8px
阴影偏移: 3px
```

---

## 7. 性能优化

### 7.1 显示优化
- 限制刷新率为100ms
- 使用部分刷新代替全屏刷新
- 优化圆角绘制算法
- 减少颜色插值计算

### 7.2 内存优化
- 静态分配大数据结构
- 避免频繁的new/delete
- 使用F()宏存储字符串常量
- 关闭不需要的省电模式

### 7.3 蓝牙优化
- 使用通知而非轮询
- 最小化数据复制
- 使用DMA传输

### 7.4 代码优化
- 使用inline函数
- 避免浮点运算
- 使用位运算
- 减少函数调用层次

---

## 8. 调试指南

### 8.1 串口调试
```cpp
Serial.begin(115200);
Serial.println("System started");

void debugPrint(const char* msg) {
    #ifdef DEBUG_MODE
    Serial.println(msg);
    #endif
}
```

### 8.2 HEX转储
```cpp
void hexDump(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) Serial.println();
    }
}
```

### 8.3 性能分析
```cpp
unsigned long startTime, endTime;
startTime = micros();
performOperation();
endTime = micros();
Serial.printf("Duration: %lu us\n", endTime - startTime);
```

### 8.4 常见问题
```
Q: 蓝牙连接不稳定
A: 降低扫描频率，增加重连机制

Q: 屏幕闪烁
A: 使用双缓冲，增加刷新间隔

Q: 数据延迟
A: 优化协议解析，减少数据处理时间

Q: 内存不足
A: 减少缓存大小，关闭PSRAM
```

---

## 附录

### A. 引脚映射表
| 功能 | GPIO | 说明 |
|------|------|------|
| 屏幕CS | 15 | 片选 |
| 屏幕DC | 2 | 数据/命令 |
| 屏幕RST | 4 | 复位 |
| 屏幕MOSI | 23 | 数据 |
| 屏幕SCLK | 18 | 时钟 |
| 背光 | 21 | 背光控制 |

### B. 寄存器配置
详细的屏幕初始化序列请参考ST7789数据手册。

### C. 参考资源
- ESP32技术文档
- ST7789数据手册
- JK BMS通信协议
- TFT_eSPI库文档

---

文档版本: 1.0.0
最后更新: 2024年
