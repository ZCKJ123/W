$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
& .\wen.exe '工具\自举字典运行时.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举字典 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\dict.exe' -PassThru -Wait
if ($p.ExitCode -ne 85) { throw "字典运行时错误：$($p.ExitCode)" }
Write-Output '自举运行时：字典新建/设/取/有键 通过'
