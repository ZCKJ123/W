@echo off
chcp 65001 >nul
setlocal
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "GXX=C:\msys64\ucrt64\bin\g++.exe"
if not exist "%GXX%" set "GXX=g++"
"%GXX%" -std=c++17 -O3 -static -municode -Wl,--stack,16777216 -o wen_full.exe wen.cpp -lwinhttp -lwinmm -lgdi32 -lpsapi -limm32
if errorlevel 1 (
    echo 完整构建失败。
    exit /b 1
)
echo 完整构建成功：wen_full.exe
call "%~dp0构建-32.bat"
if errorlevel 1 (
    echo 32 位构建失败。
    exit /b 1
)
echo 文语言 WEN-LANG-0.2（64 位 + 32 位）
