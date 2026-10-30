$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$src = Join-Path $env:TEMP 'wen_eval_check.wen'
@'
依赖 自举求值
打印 求值("1 + 2 * 3")
打印 求值("10 / 3")
打印 求值("7 % 3")
打印 求值("1 < 2 且 非 假")
'@ | Set-Content -Path $src -Encoding UTF8
$out = & .\wen.exe $src 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举求值器运行失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() })
if ($lines[0] -ne '7') { throw "1+2*3 错误：$($lines[0])" }
if ($lines[1] -ne '3.333333333333333') { throw "10/3 错误：$($lines[1])" }
if ($lines[2] -ne '1') { throw "7%3 错误：$($lines[2])" }
if ($lines[3] -ne '1') { throw "逻辑错误：$($lines[3])" }
Remove-Item $src -Force -ErrorAction SilentlyContinue
Write-Output '自举求值器：算术/除法/取余/比较/逻辑 通过'
