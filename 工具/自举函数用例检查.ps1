$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$wanted = @('25','120','十二点','2','4','36')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举函数用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举函数用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
# 求值器把多参打印连在同一行，这里 按空白拆词，逐词比对。
$got = @($out | ForEach-Object { "$_".Trim() })
$got = @($out | ForEach-Object { "$_".Trim() } | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($i -ge $got.Count) { throw "输出不足，缺少：$($wanted[$i])" }
    if ($got[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($got[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：函数值/闭包/无括号调用/多参数 通过'
