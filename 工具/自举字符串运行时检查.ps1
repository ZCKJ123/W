$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字符串运行时.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字符串 PE 生成失败' }
# 期望值口径：编译产物里 字符串长 按字节算（「甲乙丙」= 9 字节），
# 字符串取(文,0) 取首字节 0xE7 = 231，合计 240。原来写的 100 是按字符数算的旧口径。
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\str.exe' -PassThru -Wait
if ($p.ExitCode -ne 240) { throw "字符串运行时错误：$($p.ExitCode)" }
Write-Output '自举运行时：字符串字面量/长度/取字节 通过'
