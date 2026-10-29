$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$wen = Join-Path $root 'wen.exe'
$src = Join-Path $env:TEMP 'wen_native_exe_check.wen'
$exe = Join-Path $env:TEMP 'wen_native_exe_check.exe'
$exe32 = Join-Path $env:TEMP 'wen_native_exe_check32.exe'
$txt = Join-Path $env:TEMP 'wen_native_exe_text.wen'
$txtOut = Join-Path $env:TEMP 'wen_native_exe_text.out'
Set-Content -Path $src -Value '返回 42' -Encoding UTF8
Set-Content -Path $txt -Value '打印 "文语言"' -Encoding UTF8
Add-Content -Path $txt -Value '返回 7' -Encoding UTF8

& $wen --原生EXE $src $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 生成失败' }
$p = Start-Process -FilePath $exe -PassThru -Wait
if ($p.ExitCode -ne 42) { throw "原生EXE 退出码错误：$($p.ExitCode)" }

& $wen --原生EXE $txt $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 文本生成失败' }
$pt = Start-Process -FilePath $exe -PassThru -Wait -RedirectStandardOutput $txtOut
if ($pt.ExitCode -ne 7) { throw "原生EXE 文本退出码错误：$($pt.ExitCode)" }
$got = Get-Content $txtOut -Raw -Encoding UTF8
if ($got -notmatch '文语言') { throw '原生EXE 打印文本失败' }

& $wen --原生EXE32 $src $exe32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE32 生成失败' }
$p32 = Start-Process -FilePath $exe32 -PassThru -Wait
if ($p32.ExitCode -ne 42) { throw "原生EXE32 退出码错误：$($p32.ExitCode)" }

Remove-Item $src, $exe, $exe32, $txt, $txtOut -Force -ErrorAction SilentlyContinue
Write-Output '原生EXE：x64 文本/退出码、x86 退出码 通过'
