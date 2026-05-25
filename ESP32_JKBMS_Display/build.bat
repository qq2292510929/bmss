@echo off
REM ESP32 JK BMS Display Build Script for Windows
REM 中文注释: Windows平台构建脚本

echo ===========================================
echo ESP32 JK BMS Display 编译脚本
echo ===========================================
echo.

cd /d "%~dp0"

if "%1"=="clean" (
    echo 清理构建文件...
    pio run --target clean
    if %ERRORLEVEL% EQU 0 (
        echo 清理完成
    )
    goto end
)

if "%1"=="upload" (
    echo 上传程序到ESP32...
    pio run --target upload
    goto end
)

if "%1"=="monitor" (
    echo 打开串口监视器 ^(115200 波特率^)...
    pio device monitor --baud 115200
    goto end
)

echo 用法:
echo   build.bat          - 仅编译
echo   build.bat upload   - 编译并上传
echo   build.bat monitor  - 打开串口监视器
echo   build.bat clean    - 清理构建文件
echo.

echo 开始编译...
pio run

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ==========================================
    echo 编译成功!
    echo ==========================================
    echo.
    echo 固件位置: .pio\build\esp32dev\firmware.bin
    echo.
    echo 下一步:
    echo   1. 连接ESP32开发板
    echo   2. 运行: build.bat upload
    echo.
) else (
    echo.
    echo ==========================================
    echo 编译失败
    echo ==========================================
)

:end
pause
