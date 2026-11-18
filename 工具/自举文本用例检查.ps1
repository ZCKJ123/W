$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举文本用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举文本用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines[0] -ne '你好，小明！明年 19 岁') { throw "f-string 错误：$($lines[0])" }
# 最后一行是 打印 "中文"[0:1], "中文"[::-1]，两个值连成一行输出「中 文中」，
# 原来写成 '中文' 是把两段拼接后的样子当成了期望。
if ($lines[-1] -ne '中 文中') { throw "文本切片错误：$($lines[-1])" }
Write-Output '自举求值器：07文本 完整通过（f-string/三引号/查找替换分割/切片）'
