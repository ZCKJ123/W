$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$wen = Join-Path $root 'wen.exe'
$src = Join-Path $env:TEMP 'wen_native_exe_check.wen'
$exe = Join-Path $env:TEMP 'wen_native_exe_check.exe'
$exe32 = Join-Path $env:TEMP 'wen_native_exe_check32.exe'
Set-Content -Path $src -Value '返回 42' -Encoding UTF8

& $wen --原生EXE $src $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 生成失败' }
$p = Start-Process -FilePath $exe -PassThru -Wait
if ($p.ExitCode -ne 42) { throw "原生EXE 退出码错误：$($p.ExitCode)" }

& $wen --原生EXE32 $src $exe32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE32 生成失败' }
$p32 = Start-Process -FilePath $exe32 -PassThru -Wait
if ($p32.ExitCode -ne 42) { throw "原生EXE32 退出码错误：$($p32.ExitCode)" }

Remove-Item $src, $exe, $exe32 -Force -ErrorAction SilentlyContinue
Write-Output '原生EXE：独立 x64 + x86 PE 生成/运行/退出码 通过'
