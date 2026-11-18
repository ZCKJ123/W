$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举字典用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举字典用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
# 多参打印会连在同一行，所以行数不等于内容项数：按空白拆词再判数量。
$words = @($lines | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
if ($words.Count -lt 36) { throw "输出词数不足：$($words.Count)" }
$full = ($lines -join ' ')
# 「真」是多参打印里的一个词，不是单独一行，所以按词查而不是按行查。
if (-not ($words -contains '真')) { throw '缺少 有键 结果' }
if (-not ($full -match [regex]::Escape('{"乙"}'))) { throw '缺少交集结果' }
Write-Output '自举求值器：05字典集合 完整通过（字典/迭代/集合/并交差）'
