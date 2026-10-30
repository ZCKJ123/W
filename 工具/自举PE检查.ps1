$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Path 'C:/Temp/wen_selfhost' -Force | Out-Null
$exe = Join-Path $env:TEMP 'wen_selfhost\wen_selfhost_check.exe'
$src = Join-Path $env:TEMP 'wen_selfhost_check.wen'
$cases = @(
    @{ 源 = '返回 7'; 期望 = 7 },
    @{ 源 = '返回 1 + 2 * 3'; 期望 = 7 },
    @{ 源 = '返回 10 - 2 * 3'; 期望 = 4 }
)
foreach ($case in $cases) {
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
    $code = $case.源.Replace('\', '\\').Replace('"', '\"')
    $content = @"
依赖 自举库
依赖 PE代码库
将 码 变成 编译源字节("$code")
打包代码PE("$($exe.Replace('\','/'))", 码)
"@
    $content | Set-Content -Path $src -Encoding UTF8
    & .\wen.exe $src | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "自举 PE 生成失败：$($case.源)" }
    if (-not (Test-Path $exe)) { throw "自举 PE 未生成：$($case.源)" }
    $p = Start-Process -FilePath $exe -PassThru -Wait
    if ($p.ExitCode -ne $case.期望) { throw "自举 PE 退出码错误：$($case.源) → $($p.ExitCode)，期望 $($case.期望)" }
}
$extra = @(
    @{ 脚本 = '工具/自举分支.wen'; 程序 = 'C:/Temp/wen_selfhost/self_branch.exe'; 期望 = 3 },
    @{ 脚本 = '工具/自举循环.wen'; 程序 = 'C:/Temp/wen_selfhost/self_loop.exe'; 期望 = 6 },
    @{ 脚本 = '工具/自举递归.wen'; 程序 = 'C:/Temp/wen_selfhost/self_fact.exe'; 期望 = 120 },
    @{ 脚本 = '工具/自举参数.wen'; 程序 = 'C:/Temp/wen_selfhost/self_args.exe'; 期望 = 7 },
    @{ 脚本 = '工具/自举默认参数.wen'; 程序 = 'C:/Temp/wen_selfhost/self_default.exe'; 期望 = 6 },
    @{ 脚本 = '工具/自举字符串长度.wen'; 程序 = 'C:/Temp/wen_selfhost/self_len.exe'; 期望 = 2 },
    @{ 脚本 = '工具/自举睡眠探针.wen'; 程序 = 'C:/Temp/wen_selfhost/self_sleep.exe'; 期望 = 0 },
    @{ 脚本 = '工具/自举除法.wen'; 程序 = 'C:/Temp/wen_selfhost/self_div.exe'; 期望 = 3 },
    @{ 脚本 = '工具/自举取余.wen'; 程序 = 'C:/Temp/wen_selfhost/self_mod.exe'; 期望 = 1 },
    @{ 脚本 = '工具/自举布尔.wen'; 程序 = 'C:/Temp/wen_selfhost/self_bool.exe'; 期望 = 1 },
    @{ 脚本 = '工具/自举中断.wen'; 程序 = 'C:/Temp/wen_selfhost/self_break.exe'; 期望 = 3 }
)
foreach ($e in $extra) {
    Remove-Item $e.程序 -Force -ErrorAction SilentlyContinue
    & .\wen.exe $e.脚本 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "自举生成失败：$($e.脚本)" }
    $p = Start-Process -FilePath $e.程序 -PassThru -Wait
    if ($p.ExitCode -ne $e.期望) { throw "自举退出码错误：$($e.脚本) → $($p.ExitCode)" }
    Remove-Item $e.程序 -Force -ErrorAction SilentlyContinue
}
$dump = (& 'C:\msys64\ucrt64\bin\objdump.exe' -p $exe 2>&1) -join "`n"
if ($dump -match 'msvcrt|libstdc\+\+|ucrtbase') { throw '自举 PE 仍依赖 C 运行时' }
if ($dump -notmatch 'KERNEL32.dll' -or $dump -notmatch 'ExitProcess') { throw '自举 PE 导入表不完整' }
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
Write-Output '自举 PE：常量/算术/分支/循环/递归/局部变量/双参数/默认参数/字符串长度/外部 Sleep 调用/除法/取余/布尔/中断 14 例通过，退出码正确，无 C 运行时'
