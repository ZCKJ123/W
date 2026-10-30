$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举列表字面量.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举列表字面量 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\lit2.exe' -PassThru -Wait
if ($p.ExitCode -ne 11) { throw "列表字面量错误：$($p.ExitCode)" }
Write-Output '自举运行时：列表字面量/下标赋值/长度 通过'
