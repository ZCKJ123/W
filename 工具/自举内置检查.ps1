$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举内置求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举内置求值失败：$out" }
$line = @($out | ForEach-Object { "$_".Trim() })[0]
if ($line -ne '5') { throw "长度/范围错误：$line" }
Write-Output '自举求值器：长度/范围 内置函数 通过'
