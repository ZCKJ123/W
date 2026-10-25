$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
function 失败($msg) { Write-Host "发货检查失败：$msg" -ForegroundColor Red; exit 1 }

Write-Host "1/5 测试"
powershell -NoProfile -ExecutionPolicy Bypass -File 测试\运行.ps1
if ($LASTEXITCODE -ne 0) { 失败 "测试不通过" }

Write-Host "2/5 构建"
cmd /c 完整构建.bat | Out-Null
if ($LASTEXITCODE -ne 0) { 失败 "完整构建失败" }
cmd /c 轻量构建.bat | Out-Null
if ($LASTEXITCODE -ne 0) { 失败 "轻量构建失败" }

Write-Host "3/5 版本 / 签名"
$version = (.\wen_full.exe --版本) -join "`n"
if ($version -notmatch '文语言 v0\.1 · 作者 Akm / 筑宸科技 · 2026-10-25 首次发布') { 失败 "版本签名不对：$version" }
$sign = .\wen_full.exe --签名
if ($sign -notmatch 'WEN-LANG-0\.1') { 失败 "内置签名不对" }

Write-Host "4/5 二进制标记"
foreach ($exe in @('wen_full.exe', 'wen_lite.exe')) {
    $a = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path $exe)))
    if (-not $a.Contains('WEN-EMBEDDED-MARK')) { 失败 "$exe 缺少内嵌标记" }
}

Write-Host "5/5 库检查"
$libs = (.\wen_full.exe --库) -join "`n"
foreach ($name in @('时间库', '编码库', '正则库', 'CPU库', '路径库', '日志库', '哈希库')) {
    if ($libs -notmatch $name) { 失败 "缺少 $name" }
}
if ($libs -match 'GPU库') { 失败 "V0.1 不应带 GPU 库" }

Write-Host "发货检查通过：80/80，版本、签名、标记、库都正常。" -ForegroundColor Green
