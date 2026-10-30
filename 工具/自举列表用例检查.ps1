$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举列表用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举列表用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines.Count -lt 20) { throw "输出行数不足：$($lines.Count)" }
if ($lines[-1] -ne '30') { throw "末尾应为 30，实际 $($lines[-1])" }
if (-not ($lines -contains '[3, 2, 1]')) { throw '缺少反转结果 [3, 2, 1]' }
if (-not ($lines -contains '["丙", "乙", "插入", "甲"]')) { throw '缺少倒序切片结果' }
Write-Output '自举求值器：04列表 完整通过（切片/矩阵/插入/排序/包含/复合赋值）'
