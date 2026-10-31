$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$src = Join-Path $env:TEMP 'wen_selfhost\exit2k_build.wen'
@'
依赖 文件库
依赖 PE最小库
打包单节退出PE("C:/Temp/wen_selfhost/exit2k.exe", "B92A0000004883EC08FF1535000100")
'@ | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -ne 0) { throw '最小导入 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\exit2k.exe' -PassThru -Wait
if ($p.ExitCode -ne 42) { throw "最小导入 PE 错误：$($p.ExitCode)" }
Write-Output 'PE最小库：两节/导入/ExitProcess(42) 通过'
