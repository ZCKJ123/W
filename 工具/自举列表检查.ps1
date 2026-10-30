$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$wanted = @('乙','丙','3','9')
$out = & .\wen.exe '工具\自举列表求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举列表求值失败：$out" }
$got = @($out | ForEach-Object { "$_".Trim() })
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($got[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($got[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：列表字面量/下标/负下标 通过'
