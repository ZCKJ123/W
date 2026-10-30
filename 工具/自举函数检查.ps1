$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举函数求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举函数求值失败：$out" }
$line = @($out | ForEach-Object { "$_".Trim() })[0]
if ($line -ne '125') { throw "递归/默认参数错误：$line" }
Write-Output '自举求值器：函数/递归/默认参数 通过'
