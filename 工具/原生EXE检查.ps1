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

& $wen --原生EXE32 $txt $exe32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE32 文本生成失败' }
$pt32 = Start-Process -FilePath $exe32 -PassThru -Wait -RedirectStandardOutput $txtOut
if ($pt32.ExitCode -ne 7) { throw "原生EXE32 文本退出码错误：$($pt32.ExitCode)" }
$got32 = Get-Content $txtOut -Raw -Encoding UTF8
if ($got32 -notmatch '文语言') { throw '原生EXE32 打印文本失败' }

$coll = Join-Path $env:TEMP 'wen_native_exe_coll.wen'
Set-Content -Path $coll -Value '返回 [10, 20, 30][1] + {"甲": 5}["甲"]' -Encoding UTF8

& $wen --原生EXE $coll $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 容器常量生成失败' }
$pc = Start-Process -FilePath $exe -PassThru -Wait
if ($pc.ExitCode -ne 25) { throw "原生EXE 容器常量退出码错误：$($pc.ExitCode)" }

& $wen --原生EXE32 $coll $exe32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE32 容器常量生成失败' }
$pc32 = Start-Process -FilePath $exe32 -PassThru -Wait
if ($pc32.ExitCode -ne 25) { throw "原生EXE32 容器常量退出码错误：$($pc32.ExitCode)" }

Remove-Item $src, $exe, $exe32, $txt, $txtOut, $coll -Force -ErrorAction SilentlyContinue
Write-Output '原生EXE：x64/x86 文本、退出码、列表/字典常量 通过'
