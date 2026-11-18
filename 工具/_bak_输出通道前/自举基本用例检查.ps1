$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举基本用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举基本用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines.Count -lt 20) { throw "输出行数不足：$($lines.Count)" }
if ($lines[0] -ne '10') { throw "首行错误：$($lines[0])" }
if ($lines[-1] -ne '3') { throw "末行错误：$($lines[-1])" }
if (-not ($lines -contains '集合')) { throw '缺少 集合 类型结果' }
Write-Output '自举求值器：01基本 完整通过'
