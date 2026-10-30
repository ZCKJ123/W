$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$wanted = @('15','1','2','4','5','及格','单行块')
$out = & .\wen.exe '工具\自举分支求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举分支求值失败：$out" }
$got = @($out | ForEach-Object { "$_".Trim() })
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($i -ge $got.Count) { throw "输出不足，缺少：$($wanted[$i])" }
    if ($got[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($got[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：对于/继续/中断/否则如果 通过'
