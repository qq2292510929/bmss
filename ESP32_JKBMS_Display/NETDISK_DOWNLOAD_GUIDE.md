# 🎯 ESP32 JK BMS 显示系统 - 网盘下载指南

## 📦 文件信息

```
文件名: ESP32_JKBMS_Display.zip
大小: 126 KB
包含: 全部31个文件
格式: ZIP压缩包
```

---

## 🌐 网盘下载方案

### 方案1: 坚果云（推荐）

#### 步骤1: 上传ZIP到坚果云

1. 下载本地的 `ESP32_JKBMS_Display.zip`
2. 登录坚果云: https://www.jianguoyun.com/
3. 上传ZIP文件到坚果云
4. 创建分享链接
5. 将分享链接发送给我或其他需要的人

#### 步骤2: 从坚果云下载

1. 访问分享链接
2. 下载ZIP文件到本地
3. 解压到目标位置
4. 开始使用

---

### 方案2: 其他网盘服务

你也可以使用以下网盘：

- **百度网盘**: https://pan.baidu.com/
- **腾讯微云**: https://www.weiyun.com/
- **阿里云盘**: https://www.aliyundrive.com/
- **夸克网盘**: https://drive.quark.cn/
- **115网盘**: https://115.com/

#### 上传步骤（通用）

1. 在网盘上创建新文件夹: `ESP32_JKBMS_Display`
2. 上传 `ESP32_JKBMS_Display.zip` 到该文件夹
3. 或者上传整个解压后的文件夹
4. 创建分享链接
5. 保存分享链接

---

### 方案3: 直接文件传输

如果你是从我的环境获取文件：

#### 方法A: Base64编码传输

1. 我会提供Base64编码的内容
2. 你复制编码内容
3. 在本地解码为ZIP文件

#### 方法B: 分卷传输

如果ZIP文件太大，可以分卷传输：
- Part 1: 核心源代码
- Part 2: 配置文件
- Part 3: 文档

---

## 📥 获取完整Base64编码

### 如何获取

我会提供完整的Base64编码字符串，你可以：

```bash
# 保存编码内容到文件
cat > encoded.txt << 'EOF'
[这里是我的Base64编码内容]
EOF

# 解码为ZIP文件
base64 -d encoded.txt > ESP32_JKBMS_Display.zip

# 解压
unzip ESP32_JKBMS_Display.zip
```

### Base64内容

[我会在这里提供完整的Base64编码]

---

## 🖥️ Windows用户

### 方法1: PowerShell解码

```powershell
# 1. 创建编码文件
notepad encoded.txt
# 粘贴Base64内容，保存

# 2. 解码
$base64 = Get-Content encoded.txt -Raw
$bytes = [Convert]::FromBase64String($base64)
[IO.File]::WriteAllBytes("ESP32_JKBMS_Display.zip", $bytes)

# 3. 解压
Expand-Archive ESP32_JKBMS_Display.zip -DestinationPath .
```

### 方法2: 手动解压

1. 下载ZIP文件
2. 右键选择"解压到当前文件夹"
3. 或使用7-Zip/WinRAR解压

---

## 🍎 Mac用户

### 方法1: 终端解码

```bash
# 1. 保存编码
pbcopy < encoded.txt

# 2. 解码
base64 -d -i encoded.txt -o ESP32_JKBMS_Display.zip

# 3. 解压
unzip ESP32_JKBMS_Display.zip
```

### 方法2: 双击解压

1. 下载ZIP
2. 双击自动解压
3. 拖动文件夹到目标位置

---

## 🐧 Linux用户

```bash
# 1. 保存编码
vim encoded.txt

# 2. 解码
base64 -d encoded.txt > ESP32_JKBMS_Display.zip

# 3. 解压
unzip ESP32_JKBMS_Display.zip

# 4. 进入目录
cd ESP32_JKBMS_Display
```

---

## 📱 手机用户

### Android

1. 使用文件管理器打开编码文件
2. 复制Base64内容
3. 使用"Base64解码"应用
4. 保存解码后的文件
5. 解压ZIP

### iOS

1. 复制Base64内容
2. 使用"Documents"应用
3. 创建新文档，粘贴内容
4. 使用Base64解码功能
5. 保存并解压

---

## ✅ 下载后操作

### 1. 解压文件

```bash
# Linux/Mac
unzip ESP32_JKBMS_Display.zip
cd ESP32_JKBMS_Display

# Windows
# 双击ZIP文件，或右键"解压全部"
```

### 2. 验证文件

```bash
# 检查文件
ls -la

# 应该看到:
# ├── src/
# ├── lib/
# ├── docs/
# ├── platformio.ini
# ├── README.md
# └── ...
```

### 3. 开始使用

```bash
# 使用PlatformIO编译
pio run

# 或使用Arduino IDE
# 打开 src/main.cpp
```

---

## 🔧 快速开始

### PlatformIO方式

```bash
# 1. 进入目录
cd ESP32_JKBMS_Display

# 2. 编译
pio run

# 3. 上传
pio run --target upload

# 4. 查看输出
pio device monitor
```

### Arduino IDE方式

1. 解压文件
2. 打开 Arduino IDE
3. File → Open → 选择 `src/main.cpp`
4. 安装必要的库（TFT_eSPI, NimBLE）
5. Tools → Board → ESP32 Dev Module
6. Upload

---

## 📚 文件清单

解压后的完整文件列表：

```
ESP32_JKBMS_Display/
├── src/
│   ├── main.cpp
│   ├── JKBMS_Protocol.h
│   ├── JKBMS_Protocol.cpp
│   ├── JKBMS_Bluetooth.h
│   ├── JKBMS_Bluetooth.cpp
│   ├── Battle_UI.h
│   ├── Battle_UI.cpp
│   ├── Config.h
│   ├── Logger.h
│   ├── Logger.cpp
│   ├── DebugUtils.h
│   ├── DebugUtils.cpp
│   └── ExampleConfig.h
├── lib/
│   └── TFT_eSPI/
│       ├── User_Setup_Select.h
│       ├── User_Config.h
│       └── Setup.h
├── docs/
│   ├── TECHNICAL.md
│   └── UI_DEMO.md
├── platformio.ini
├── README.md
├── QUICK_START.md
├── INSTALL_GUIDE.md
├── DOWNLOAD_GUIDE.md
├── GITHUB_UPLOAD_GUIDE.md
├── DELIVERY_CHECKLIST.txt
├── PROJECT_SUMMARY.md
├── PROJECT_STRUCTURE.md
└── README_INDEX.md
```

---

## 🆘 常见问题

### Q1: ZIP文件下载失败？

**解决方法:**
- 检查网络连接
- 更换下载工具
- 使用浏览器下载
- 尝试其他网盘

### Q2: 解压报错？

**解决方法:**
- 检查ZIP完整性
- 使用专业解压工具（7-Zip, WinRAR）
- 重新下载
- 尝试命令行解压

### Q3: 文件损坏？

**解决方法:**
- 重新下载
- 检查下载过程
- 验证文件大小
- 使用MD5校验

---

## 📞 获取帮助

如果遇到问题：

1. **下载问题**: 检查网盘链接
2. **解压问题**: 使用专业工具
3. **编译问题**: 查看错误日志
4. **其他问题**: 告诉我具体情况

---

## 🎉 完成

下载并解压后，你就可以开始使用了！

记得：
1. ✅ 安装PlatformIO或Arduino IDE
2. ✅ 连接硬件
3. ✅ 编译上传
4. ✅ 享受战斗模式UI！

---

**祝你使用愉快！⚡🔋**

---

## 📋 下载检查清单

下载前确认：
- [ ] 网盘已安装/可访问
- [ ] 有解压工具（7-Zip, WinRAR等）
- [ ] 有126KB以上存储空间
- [ ] 可以访问GitHub（备用方案）

下载后确认：
- [ ] ZIP文件完整（126KB左右）
- [ ] 解压成功
- [ ] 看到31个文件
- [ ] 包含 src/ lib/ docs/ 目录
