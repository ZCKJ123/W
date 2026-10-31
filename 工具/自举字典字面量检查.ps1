$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字典字面量.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字典字面量 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\dlit.exe' -PassThru -Wait
if ($p.ExitCode -ne 85) { throw "字典字面量错误：$($p.ExitCode)" }
Write-Output '自举编译器：字典字面量/键访问 通过'
