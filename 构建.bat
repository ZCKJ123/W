@echo off
chcp 65001 >nul
setlocal
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
set "GXX=C:\msys64\ucrt64\bin\g++.exe"
if not exist "%GXX%" set "GXX=g++"
rem 先编到 wen.exe.new：编失败时不会把现有的 wen.exe 弄没（g++ 链接失败会删除 -o 目标）
"%GXX%" -std=c++17 -O3 -mavx2 -mfma -fno-math-errno -fno-trapping-math -fopenmp -static -municode -Wl,--stack,16777216 -o wen.exe.new wen.cpp -lwinhttp -lwinmm -lmfplat -lmfreadwrite -lmfuuid -lole32 -lgdi32 -lpsapi -limm32 -lws2_32
if errorlevel 1 (
    echo 构建失败，保留原来的 wen.exe。
    del /q wen.exe.new 2>nul
    exit /b 1
)
move /y wen.exe.new wen.exe >nul
if errorlevel 1 (
    echo 替换失败：wen.exe 被别的进程占用，新产物留在 wen.exe.new。
    echo 关掉正在运行的 wen.exe 后重跑本脚本，或手动把 wen.exe.new 改名成 wen.exe。
    exit /b 1
)
echo 构建成功：wen.exe
echo 文语言 WEN-LANG-0.3
