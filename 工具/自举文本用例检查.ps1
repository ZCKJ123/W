$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举文本用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举文本用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
if ($lines[0] -ne '你好，小明！明年 19 岁') { throw "f-string 错误：$($lines[0])" }
if ($lines[-1] -ne '中文') { throw "文本切片错误：$($lines[-1])" }
Write-Output '自举求值器：07文本 完整通过（f-string/三引号/查找替换分割/切片）'
