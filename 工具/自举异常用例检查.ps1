$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$out = & .\wen.exe '工具\自举异常用例求值.wen' 2>&1
if ($LASTEXITCODE -ne 0) { throw "自举异常用例失败：$out" }
$lines = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
$wanted = @('运行时错误','故意出错','最终块','除零：','除数不能为 0','3','失败')
for ($i = 0; $i -lt $wanted.Count; $i++) {
    if ($i -ge $lines.Count) { throw "输出不足，缺少：$($wanted[$i])" }
    if ($lines[$i] -ne $wanted[$i]) { throw "第 $i 行错误：$($lines[$i])，期望 $($wanted[$i])" }
}
Write-Output '自举求值器：06异常 完整通过（尝试/捕获/最终/除零/函数内捕获）'
