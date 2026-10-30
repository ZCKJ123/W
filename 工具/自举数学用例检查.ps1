$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举数学用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举数学用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines.Count -lt 15) { throw "输出行数不足：$($lines.Count)" }
if ($lines[0] -ne '3628800') { throw "阶乘错误：$($lines[0])" }
if ($lines[-1] -ne '2.00') { throw "格式化错误：$($lines[-1])" }
Write-Output '自举求值器：08数学 完整通过'
