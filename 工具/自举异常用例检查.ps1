$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
# 跑之前先删输出文件
Remove-Item -LiteralPath 'C:\Temp\wen_out.txt' -Force -ErrorAction SilentlyContinue
$out = & .\wen.exe '工具\自举异常用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举异常用例失败：$out" }
# 求值器的打印走 输出文本，落到 C:\Temp\wen_out.txt（宿主没有无换行写 stdout 的内置）；
# 所以比对对象从 wen.exe 的 stdout 换成这个输出文件。
if (Test-Path 'C:\Temp\wen_out.txt') { $out = @(Get-Content -LiteralPath 'C:\Temp\wen_out.txt' -Encoding utf8) }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
# 期望列表是逐词写的，而输出是多参打印连成的一行（运行时错误 故意出错 / 除零： 除数不能为 0），
# 所以先按空白拆词，再逐项比对。
$words = @($lines | ForEach-Object { $_ -split ' ' } | Where-Object { $_ -ne '' })
$wanted = @('运行时错误','故意出错','最终块','除零：','除数不能为','0','3','失败')
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($i -ge $words.Count) { throw "输出不足，缺少：$($wanted[$i])" }
    if ($words[$i] -ne $wanted[$i]) { throw "第 $i 项错误：$($words[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：06异常 完整通过（尝试/捕获/最终/除零/函数内捕获）'
