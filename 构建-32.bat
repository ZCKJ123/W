@echo off
chcp 65001 >nul
setlocal
set "PATH=C:\msys64\mingw32\bin;%PATH%"
set "GXX=C:\msys64\mingw32\bin\g++.exe"
if not exist "%GXX%" (
    echo 找不到 32 位编译器：%GXX%
    exit /b 1
)
"%GXX%" -std=c++17 -O3 -msse2 -march=core2 -static -municode -Wl,--stack,16777216 -o wen32.exe wen.cpp -lwinhttp -lgdi32 -lpsapi -limm32
if errorlevel 1 (
    echo 32 位构建失败。
    exit /b 1
)
echo 32 位构建成功：wen32.exe
echo 文语言 WEN-LANG-0.2
