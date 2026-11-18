$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举数学用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举数学用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
# 08数学.wen 一共 9 条 打印，多参的在输出里连在同一行；
# 按行数卡 15 是错的（那是把多参拆开之后的数量），这里先按空白拆词再数。
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' } | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
if ($lines.Count -lt 15) { throw "输出行数不足：$($lines.Count)" }
if ($lines[0] -ne '3628800') { throw "阶乘错误：$($lines[0])" }
if ($lines[-1] -ne '2.00') { throw "格式化错误：$($lines[-1])" }
Write-Output '自举求值器：08数学 完整通过'
