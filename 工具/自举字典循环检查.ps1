$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字典循环.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字典循环 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\dfor.exe' -PassThru -Wait
if ($p.ExitCode -ne 175) { throw "字典循环错误：$($p.ExitCode)" }
Write-Output '自举编译器：字典 对于 通过'
