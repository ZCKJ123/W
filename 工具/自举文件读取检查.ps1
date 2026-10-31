$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$f = Join-Path $env:TEMP 'wen_read_test.txt'
[IO.File]::WriteAllText($f, 'hello', [Text.Encoding]::ASCII)
& .\wen.exe "$env:TEMP\file2.wen" | Out-Null
if ($LASTEXITCODE -ne 0) { throw '文件读取 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\file2.exe' -PassThru -Wait
if ($p.ExitCode -ne 604) { throw "文件读取错误：$($p.ExitCode)" }
Write-Output '自举运行时：打开/大小/读入/关闭 通过'
