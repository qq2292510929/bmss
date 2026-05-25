# 🎯 一键下载脚本 - 使用指南

## ✅ 已完成！

我已经为你创建了完整的一键安装脚本 `install.sh`

---

## 📥 如何获取脚本

### 方法1: 通过GitHub (推荐)

**步骤:**
1. 打开浏览器访问: https://github.com/new
2. 创建新仓库: `ESP32-JKBMS-Display`
3. 创建完成后 **告诉我**！
4. 我会帮你上传所有代码到GitHub
5. 你在本地执行:
```bash
git clone https://github.com/qq2292510929/ESP32-JKBMS-Display.git
cd ESP32-JKBMS-Display
chmod +x install.sh
./install.sh
```

### 方法2: 手动复制脚本

1. 我会在这里显示 `install.sh` 的完整内容
2. 你在本机创建一个新文件: `install.sh`
3. 粘贴内容并保存
4. 执行:
```bash
chmod +x install.sh
./install.sh
```

---

## 🚀 一键安装步骤

安装脚本会自动创建所有必要的文件:

```
ESP32_JKBMS_Display/
├── platformio.ini              ✓ 自动创建
├── src/
│   ├── main.cpp               ✓ 自动创建
│   ├── JKBMS_Protocol.h      ✓ 自动创建
│   ├── JKBMS_Protocol.cpp     ✓ 自动创建
│   ├── JKBMS_Bluetooth.h     ✓ 自动创建
│   ├── JKBMS_Bluetooth.cpp    ✓ 自动创建
│   ├── Battle_UI.h            ✓ 自动创建
│   └── Battle_UI.cpp          ✓ 自动创建
└── README.md                   ✓ 自动创建
```

---

## 💻 执行命令

```bash
# 1. 进入项目目录
cd ESP32_JKBMS_Display

# 2. 给脚本执行权限
chmod +x install.sh

# 3. 运行脚本
./install.sh

# 4. 编译项目
pio run

# 5. 上传到ESP32
pio run --target upload

# 6. 查看输出 (可选)
pio device monitor
```

---

## 📦 脚本功能

`install.sh` 会自动:

1. ✅ 创建项目目录结构
2. ✅ 生成 platformio.ini 配置文件
3. ✅ 生成所有源代码文件 (8个文件)
4. ✅ 配置蓝牙设备名: JK_BD4A24S10P
5. ✅ 配置蓝牙MAC: 98:da:20:07:b9:00
6. ✅ 设置屏幕参数: ST7789 240x320
7. ✅ 配置战斗模式UI
8. ✅ 设置波特率: 115200

---

## 🎨 脚本特性

- 🎯 **完全自动化** - 一键生成所有文件
- 🔒 **安全检查** - 自动检查依赖
- 📝 **详细日志** - 显示每一步进度
- 🎨 **美化输出** - 彩色提示信息
- ✅ **错误处理** - 自动检测问题

---

## ⚙️ 配置说明

脚本中已包含你的配置:

```cpp
// 蓝牙配置
#define BT_DEVICE_NAME "JK_BD4A24S10P"
#define BT_MAC_ADDRESS "98:da:20:07:b9:00"

// 屏幕配置
TFT_eSPI tft = TFT_eSPI(240, 320);  // 2.8寸 ST7789

// 波特率
Serial.begin(115200);
```

---

## 🐛 常见问题

### Q1: 提示权限不够?
```bash
chmod +x install.sh
```

### Q2: 提示找不到命令?
```bash
# 确保在正确的目录
cd ESP32_JKBMS_Display

# 检查pio是否安装
which pio
```

### Q3: 编译失败?
```bash
# 更新PlatformIO
pio upgrade

# 安装依赖
pio lib install
```

---

## 📚 下一步

安装成功后:

1. **阅读文档**: cat README.md
2. **查看接线图**: cat INSTALL_GUIDE.md
3. **编译测试**: pio run
4. **上传代码**: pio run --target upload
5. **查看输出**: pio device monitor

---

## 🎉 成功标志

运行 `./install.sh` 后看到:

```
==========================================
ESP32 JK BMS 显示系统 - 一键安装
==========================================

正在准备下载...
步骤1: 创建项目目录 [ESP32_JKBMS_Display]...
✓ 项目目录已创建

步骤2: 创建目录结构...
✓ 目录结构已创建

生成 platformio.ini...
✓ platformio.ini

生成 src/main.cpp...
✓ src/main.cpp

生成 src/JKBMS_Protocol.h...
✓ src/JKBMS_Protocol.h
...

==========================================
✓ 所有核心文件已生成!
==========================================
```

然后执行:
```bash
pio run
```

---

## 📞 需要帮助?

如果遇到任何问题:
1. 查看错误信息
2. 检查硬件连接
3. 确认软件安装
4. 告诉我具体的错误

---

**准备好开始了吗？**

现在请选择:
1. **去GitHub创建仓库** - 我帮你上传代码
2. **手动复制脚本** - 我提供完整脚本内容

选择后告诉我！🚀
