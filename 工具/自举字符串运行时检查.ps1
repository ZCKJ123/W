$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字符串运行时.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字符串 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\str.exe' -PassThru -Wait
if ($p.ExitCode -ne 100) { throw "字符串运行时错误：$($p.ExitCode)" }
Write-Output '自举运行时：字符串字面量/长度/取字节 通过'
