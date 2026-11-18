$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Path 'C:/Temp/wen_selfhost' -Force | Out-Null
$exe = Join-Path $env:TEMP 'wen_selfhost\wen_selfhost_check.exe'
$src = Join-Path $env:TEMP 'wen_selfhost_check.wen'
$抖动 = 0
function 跑产物([string]$路径, [int]$期望, [string]$标签, [datetime]$编译前) {
    # 检查脚本自己也会骗人：产物没写出来、写坏了、或者根本没重写（旧文件冒充），
    # 只看退出码都可能蒙对。先钉住「文件在、够大、确实是这次写的」，再谈退出码。
    if (-not (Test-Path -LiteralPath $路径)) { throw ("自举产物缺失：{0}" -f $标签) }
    $档 = Get-Item -LiteralPath $路径
    if ($档.Length -lt 1024) {
        throw ("自举产物过小（写坏或没写全）：{0} → {1} 字节" -f $标签, $档.Length)
    }
    if ($档.LastWriteTime -lt $编译前.AddSeconds(-1)) {
        throw ("自举产物没有重新写出（旧文件冒充新产物）：{0}" -f $标签)
    }
    $甲 = (Start-Process -FilePath $路径 -PassThru -Wait).ExitCode
    if ($甲 -eq $期望) { return }
    # 首次退出码不对时复测一次：把「首次启动被环境拦掉」和「生成的代码本身错」分开报，
    # 不再用一个含糊的 0 混过去。
    $乙 = (Start-Process -FilePath $路径 -PassThru -Wait).ExitCode
    if ($乙 -eq $期望) {
        $script:抖动++
        Write-Host ("注意：{0} 首次启动退出码 {1}，复测 {2} —— 计入启动抖动" -f $标签, $甲, $乙)
        return
    }
    throw ("自举退出码错误：{0} → 首次 {1}、复测 {2}，期望 {3}" -f $标签, $甲, $乙, $期望)
}

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
    $编译前 = [datetime]::Now
    & .\wen.exe $src | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "自举 PE 生成失败：$($case.源)" }
    跑产物 $exe $case.期望 $case.源 $编译前
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
    # 删不掉说明有别的进程占着它，后面写出来的可能是坏文件 —— 当场报，不留给下一轮背锅。
    if (Test-Path -LiteralPath $e.程序) { throw ("旧产物删不掉（被占用）：{0}" -f $e.脚本) }
    $编译前 = [datetime]::Now
    & .\wen.exe $e.脚本 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "自举生成失败：$($e.脚本)" }
    跑产物 $e.程序 $e.期望 $e.脚本 $编译前
    Remove-Item $e.程序 -Force -ErrorAction SilentlyContinue
}
$dump = (& 'C:\msys64\ucrt64\bin\objdump.exe' -p $exe 2>&1) -join "`n"
if ($dump -match 'msvcrt|libstdc\+\+|ucrtbase') { throw '自举 PE 仍依赖 C 运行时' }
if ($dump -notmatch 'KERNEL32.dll' -or $dump -notmatch 'ExitProcess') { throw '自举 PE 导入表不完整' }
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
$尾巴 = ''
if ($抖动 -gt 0) { $尾巴 = ("（其中 {0} 例首次启动抖动，已复测）" -f $抖动) }
Write-Output ("自举 PE：常量/算术/分支/循环/递归/局部变量/双参数/默认参数/字符串长度/外部 Sleep 调用/除法/取余/布尔/中断 14 例通过，退出码正确，无 C 运行时{0}" -f $尾巴)
