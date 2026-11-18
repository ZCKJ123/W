$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举基本用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举基本用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
# 多参打印会连在同一行，所以行数不等于内容项数：按空白拆词再判数量。
$words = @($lines | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
if ($words.Count -lt 28) { throw "输出词数不足：$($words.Count)" }
# 首行是 打印 甲+乙, 甲-乙 … 七个值连成一行，末行是 文本 42, 文本 3.5, 文本 真 三个值，
# 所以断言要看拆出来的词，不是整行。
if ($words[0] -ne '10') { throw "首行错误：$($lines[0])" }
if ($words[-1] -ne '真') { throw "末项错误：$($words[-1])" }
if (-not ($words -contains '集合')) { throw '缺少 集合 类型结果' }
Write-Output '自举求值器：01基本 完整通过'
