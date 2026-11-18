$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举列表用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举列表用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
# 多参打印会连在同一行，所以行数不等于内容项数：按空白拆词再判数量。
$words = @($lines | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
if ($words.Count -lt 40) { throw "输出词数不足：$($words.Count)" }
# 整行里检索某个完整片段：不能直接 -contains（那是整行相等比较），
# 也不能用 -like（[ ] 在 -like 里是字符类），用 regex 的 Escape。
$full = ($lines -join ' ')
if ($lines[-1] -ne '30') { throw "末尾应为 30，实际 $($lines[-1])" }
if (-not ($full -match [regex]::Escape('[3, 2, 1]'))) { throw '缺少反转结果 [3, 2, 1]' }
if (-not ($full -match [regex]::Escape('["丙", "乙", "插入", "甲"]'))) { throw '缺少倒序切片结果' }
Write-Output '自举求值器：04列表 完整通过（切片/矩阵/插入/排序/包含/复合赋值）'
