$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举函数求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举函数求值失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$line = @($out | ForEach-Object { "$_".Trim() })[0]
if ($line -ne '125') { throw "递归/默认参数错误：$line" }
Write-Output '自举求值器：函数/递归/默认参数 通过'
