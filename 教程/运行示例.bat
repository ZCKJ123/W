@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

if not exist "wen.exe" (
    echo 找不到 wen.exe。
    echo 请把本文件、wen.exe 和 示例 文件夹放在同一个目录。
    pause
    exit /b 1
)

echo.
echo ============================================
echo   文言入门示例
echo   每个程序运行完会停一下，按回车继续下一个
echo ============================================

for %%f in (示例\*.脚本) do (
    echo.
    echo --------------------------------------------
    echo 运行：%%~nxf
    echo --------------------------------------------
    wen.exe "%%f"
    echo.
    pause
)

echo.
echo 全部示例运行完毕。
echo 接下来用记事本打开 示例 里的文件，改一个数字再跑一次。
pause
