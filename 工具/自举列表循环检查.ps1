$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举列表循环.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举列表循环 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\forlist.exe' -PassThru -Wait
if ($p.ExitCode -ne 6) { throw "列表循环错误：$($p.ExitCode)" }
Write-Output '自举编译器：列表 对于 通过'
