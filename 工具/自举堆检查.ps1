$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$exe = Join-Path $env:TEMP 'wen_selfhost\heap_check.exe'
Remove-Item $exe -Force -ErrorAction SilentlyContinue
& .\wen.exe '工具\自举堆.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举堆 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\heap.exe' -PassThru -Wait
if ($p.ExitCode -ne 42) { throw "堆存取错误：$($p.ExitCode)" }
Write-Output '自举运行时：分配/存/取 通过'
