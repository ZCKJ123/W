$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字符串连接.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字符串连接 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\strcat2.exe' -PassThru -Wait
if ($p.ExitCode -ne 103) { throw "字符串连接错误：$($p.ExitCode)" }
Write-Output '自举运行时：字符串连接 通过'
