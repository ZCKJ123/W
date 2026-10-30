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

$readFile = Join-Path $env:TEMP 'wen_native_exe_read.txt'
$readSrc = Join-Path $env:TEMP 'wen_native_exe_read.wen'
$readOut = Join-Path $env:TEMP 'wen_native_exe_read.out'
Set-Content -Path $readFile -Value '文件内容' -Encoding UTF8
$readPath = $readFile.Replace('\', '/')
Set-Content -Path $readSrc -Value ('打印 读取("' + $readPath + '")') -Encoding UTF8
Add-Content -Path $readSrc -Value '返回 6' -Encoding UTF8

& $wen --原生EXE $readSrc $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 读取生成失败' }
$pr = Start-Process -FilePath $exe -PassThru -Wait -RedirectStandardOutput $readOut
if ($pr.ExitCode -ne 6) { throw "原生EXE 读取退出码错误：$($pr.ExitCode)" }
$gotRead = Get-Content $readOut -Raw -Encoding UTF8
if ($gotRead -notmatch '文件内容') { throw '原生EXE 读取输出失败' }

& $wen --原生EXE32 $readSrc $exe32 | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE32 读取生成失败' }
$pr32 = Start-Process -FilePath $exe32 -PassThru -Wait -RedirectStandardOutput $readOut
if ($pr32.ExitCode -ne 6) { throw "原生EXE32 读取退出码错误：$($pr32.ExitCode)" }
$gotRead32 = Get-Content $readOut -Raw -Encoding UTF8
if ($gotRead32 -notmatch '文件内容') { throw '原生EXE32 读取输出失败' }

$writeTarget = Join-Path $env:TEMP 'wen_native_exe_written.txt'
$writeSrc = Join-Path $env:TEMP 'wen_native_exe_write.wen'
Remove-Item $writeTarget -Force -ErrorAction SilentlyContinue
$writePath = $writeTarget.Replace('\', '/')
Set-Content -Path $writeSrc -Value ('写入("' + $writePath + '", "PE运行时写入")') -Encoding UTF8
Add-Content -Path $writeSrc -Value '返回 8' -Encoding UTF8

& $wen --原生EXE $writeSrc $exe | Out-Null
if ($LASTEXITCODE -ne 0) { throw '原生EXE 运行时写入生成失败' }
$pw = Start-Process -FilePath $exe -PassThru -Wait
if ($pw.ExitCode -ne 8) { throw "原生EXE 运行时写入退出码错误：$($pw.ExitCode)" }
$wrote = Get-Content $writeTarget -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
if ($wrote -notmatch 'PE运行时写入') { throw '原生EXE 运行时写入内容失败' }

$badSrc = Join-Path $env:TEMP 'wen_native_exe_badwrite.wen'
Set-Content -Path $badSrc -Value '写入("C:/不存在目录_xyz/bad.txt", "x")' -Encoding UTF8
Add-Content -Path $badSrc -Value '返回 0' -Encoding UTF8
& $wen --原生EXE $badSrc $exe | Out-Null
$pb = Start-Process -FilePath $exe -PassThru -Wait
if ($pb.ExitCode -ne 1) { throw "原生EXE 写入失败路径退出码错误：$($pb.ExitCode)" }

Remove-Item $src, $exe, $exe32, $txt, $txtOut, $coll, $readFile, $readSrc, $readOut, $writeTarget, $writeSrc, $badSrc -Force -ErrorAction SilentlyContinue
Write-Output '原生EXE：x64/x86 文本、退出码、列表/字典常量、编译期读取、x64 运行时写入/失败路径 通过'
