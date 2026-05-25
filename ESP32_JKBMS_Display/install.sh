#!/bin/bash

echo "======================================"
echo "ESP32 JK BMS Display 安装脚本"
echo "======================================"
echo ""

echo "步骤1: 检查Python环境..."
if command -v python3 &> /dev/null; then
    echo "✓ Python3 已安装"
    python3 --version
else
    echo "✗ Python3 未安装，请先安装Python3"
    exit 1
fi

echo ""
echo "步骤2: 安装PlatformIO..."
if command -v platformio &> /dev/null; then
    echo "✓ PlatformIO 已安装"
    platformio --version
else
    echo "正在安装PlatformIO..."
    pip install platformio
    if [ $? -eq 0 ]; then
        echo "✓ PlatformIO 安装成功"
    else
        echo "✗ PlatformIO 安装失败"
        exit 1
    fi
fi

echo ""
echo "步骤3: 安装依赖库..."
cd ESP32_JKBMS_Display
pio lib install

echo ""
echo "步骤4: 编译项目..."
pio run

if [ $? -eq 0 ]; then
    echo ""
    echo "======================================"
    echo "✓ 编译成功!"
    echo "======================================"
    echo ""
    echo "下一步操作:"
    echo "1. 使用USB线连接ESP32开发板"
    echo "2. 执行: pio run --target upload"
    echo "3. 监控输出: pio device monitor"
    echo ""
else
    echo ""
    echo "======================================"
    echo "✗ 编译失败，请检查错误信息"
    echo "======================================"
    exit 1
fi
