$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$wanted = @('乙','丙','3','9')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举列表求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举列表求值失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
# 求值器把多参打印连在同一行，这里 按空白拆词，逐词比对。
$got = @($out | ForEach-Object { "$_".Trim() })
$got = @($out | ForEach-Object { "$_".Trim() } | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($got[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($got[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：列表字面量/下标/负下标 通过'
