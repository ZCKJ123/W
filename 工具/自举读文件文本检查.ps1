$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
[IO.File]::WriteAllText('C:\Temp\test.txt', 'hello', [Text.Encoding]::ASCII)
& .\wen.exe '工具\自举读文件文本.wen' | Out-Null
if ($LASTEXITCODE -ne 0) { throw '读文件文本 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\readfn.exe' -PassThru -Wait
if ($p.ExitCode -ne 604) { throw "读文件文本错误：$($p.ExitCode)" }
Write-Output '自举运行时：读文件文本 通过'
