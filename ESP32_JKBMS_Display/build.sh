#!/bin/bash

echo "==========================================="
echo "ESP32 JK BMS Display 编译脚本"
echo "==========================================="
echo ""

if [ ! -d "ESP32_JKBMS_Display" ]; then
    echo "错误: 找不到项目目录 ESP32_JKBMS_Display"
    echo "请确保在正确的目录下运行此脚本"
    exit 1
fi

cd ESP32_JKBMS_Display

if [ "$1" == "clean" ]; then
    echo "清理构建文件..."
    pio run --target clean
    echo "清理完成"
    exit 0
fi

if [ "$1" == "upload" ]; then
    echo "上传程序到ESP32..."
    pio run --target upload --target monitor
    exit 0
fi

if [ "$1" == "monitor" ]; then
    echo "打开串口监视器 (115200 波特率)..."
    pio device monitor --baud 115200
    exit 0
fi

echo "选项:"
echo "  ./build.sh          - 仅编译"
echo "  ./build.sh upload   - 编译并上传到ESP32"
echo "  ./build.sh monitor  - 打开串口监视器"
echo "  ./build.sh clean    - 清理构建文件"
echo ""

echo "开始编译..."
pio run

if [ $? -eq 0 ]; then
    echo ""
    echo "==========================================="
    echo "✓ 编译成功!"
    echo "==========================================="
    echo ""
    echo "固件位置: .pio/build/esp32dev/firmware.bin"
    echo ""
    echo "下一步:"
    echo "  1. 连接ESP32开发板"
    echo "  2. 运行: ./build.sh upload"
    echo "  3. 运行时查看: ./build.sh monitor"
    echo ""
else
    echo ""
    echo "==========================================="
    echo "✗ 编译失败"
    echo "==========================================="
    exit 1
fi
