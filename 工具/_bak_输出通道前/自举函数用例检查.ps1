$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$wanted = @('25','120','十二点','2','4','36')
$out = & .\wen.exe '工具\自举函数用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举函数用例失败：$out" }
$got = @($out | ForEach-Object { "$_".Trim() })
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($i -ge $got.Count) { throw "输出不足，缺少：$($wanted[$i])" }
    if ($got[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($got[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：函数值/闭包/无括号调用/多参数 通过'
