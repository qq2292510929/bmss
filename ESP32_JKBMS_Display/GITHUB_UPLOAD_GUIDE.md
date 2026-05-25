# GitHub 上传指南

## 快速步骤 (约2分钟)

### 步骤1: 在GitHub创建仓库

1. 打开浏览器访问: https://github.com
2. 登录你的账号: **qq2292510929**
3. 点击右上角的 **+** 按钮
4. 选择 **New repository**

### 步骤2: 填写仓库信息

在创建仓库页面填写:

```
Repository name: ESP32-JKBMS-Display
Description: ESP32 JK BMS显示系统 - 战斗模式UI
Visibility: 选择 Private (私有) 或 Public (公开)
```

**重要**: 不要勾选以下选项:
- ❌ Initialize this repository with a README
- ❌ Add .gitignore
- ❌ Add a license

点击 **Create repository**

### 步骤3: 复制仓库地址

创建完成后，你会看到仓库页面。复制仓库的HTTPS地址:

```
https://github.com/qq2292510929/ESP32-JKBMS-Display.git
```

### 步骤4: 推送到GitHub

在你的电脑上打开终端，执行以下命令:

```bash
# 进入项目目录
cd ESP32_JKBMS_Display

# 添加远程仓库
git remote add origin https://github.com/qq2292510929/ESP32-JKBMS-Display.git

# 重命名分支为main
git branch -M main

# 推送到GitHub
git push -u origin main
```

### 步骤5: 输入认证信息

推送时需要输入GitHub认证信息:

**用户名**: `qq2292510929`

**密码**: 
- 如果你启用了两步验证，需要使用 **Personal Access Token**
- Token获取方法见下文

### 步骤6: 验证成功

刷新GitHub仓库页面，你应该能看到所有31个文件:
- ✅ src/ (12个源代码文件)
- ✅ lib/ (3个库文件)
- ✅ docs/ (2个文档)
- ✅ platformio.ini
- ✅ README.md
- ✅ 其他配置文件

---

## 获取Personal Access Token (推荐)

由于GitHub不再支持密码认证，需要使用Token。

### 创建Token步骤:

1. 访问: https://github.com/settings/tokens
2. 点击 **Generate new token (classic)**
3. 填写:
   - Note: "ESP32 Project Access"
   - Expiration: 选择 30 days 或 90 days
   - 勾选 **repo** (完整仓库访问)
4. 点击 **Generate token**
5. **重要**: 立即复制并保存Token！

### 使用Token:

当git push要求输入密码时，粘贴Token作为密码。

---

## 遇到问题？

### 问题1: 权限拒绝

**错误信息**:
```
remote: Permission denied.
fatal: Authentication failed.
```

**解决方法**:
1. 检查用户名是否正确: `qq2292510929`
2. 密码应该使用Token，不是GitHub密码
3. Token需要有repo权限

### 问题2: 仓库已存在

**错误信息**:
```
error: remote origin already exists.
```

**解决方法**:
```bash
git remote remove origin
git remote add origin https://github.com/qq2292510929/ESP32-JKBMS-Display.git
```

### 问题3: 分支冲突

**解决方法**:
```bash
git pull origin main --allow-unrelated-histories
# 或者强制推送
git push -u origin main --force
```

---

## 上传后的操作

### 1. 在VSCode中打开项目

```bash
# 安装VSCode (如果没有)
# https://code.visualstudio.com/

# 克隆仓库
git clone https://github.com/qq2292510929/ESP32-JKBMS-Display.git

# 在VSCode中打开
code ESP32-JKBMS-Display
```

### 2. 安装PlatformIO

1. 在VSCode中安装PlatformIO IDE插件
2. 等待安装完成
3. 重启VSCode

### 3. 编译项目

```bash
# 在项目目录打开终端
pio run
```

### 4. 上传到ESP32

```bash
pio run --target upload
```

---

## 项目文件结构

上传成功后，你在GitHub上会看到:

```
ESP32-JKBMS-Display/
├── src/
│   ├── main.cpp                  # 主程序
│   ├── JKBMS_Protocol.h/.cpp   # 协议解析
│   ├── JKBMS_Bluetooth.h/.cpp   # 蓝牙管理
│   ├── Battle_UI.h/.cpp          # UI界面
│   └── ... (其他工具类)
├── lib/
│   └── TFT_eSPI/                 # 屏幕驱动
├── docs/
│   ├── TECHNICAL.md              # 技术文档
│   └── UI_DEMO.md                # UI演示
├── platformio.ini               # PlatformIO配置
├── README.md                     # 项目说明
├── QUICK_START.md               # 快速开始
└── ... (其他配置文件)
```

---

## 后续同步

如果你在本地修改了代码，想要同步到GitHub:

```bash
# 添加修改
git add .

# 提交
git commit -m "更新说明"

# 推送到GitHub
git push
```

---

## 获取帮助

如果遇到问题:
1. 查看GitHub官方文档: https://docs.github.com/
2. 检查错误信息
3. 确认网络连接
4. 验证认证信息

---

祝你上传成功！有任何问题随时告诉我！🚀
