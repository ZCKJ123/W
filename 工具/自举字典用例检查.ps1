$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举字典用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举字典用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines.Count -lt 20) { throw "输出行数不足：$($lines.Count)" }
if (-not ($lines -contains '真')) { throw '缺少 有键 结果' }
if (-not ($lines -contains '{"乙"}')) { throw '缺少交集结果' }
Write-Output '自举求值器：05字典集合 完整通过（字典/迭代/集合/并交差）'
