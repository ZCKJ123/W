param([switch]$32)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = if ($32) { Join-Path $root 'wen32.exe' } else { Join-Path $root 'wen.exe' }
if (-not (Test-Path $exe)) { throw "找不到 $exe" }

function Run-Script([string]$text) {
    $p = [IO.Path]::GetTempFileName() + '.wen'
    [IO.File]::WriteAllText($p, $text, (New-Object Text.UTF8Encoding($false)))
    try {
        $t = Measure-Command { & $exe $p | Out-Null }
        return $t.TotalSeconds
    } finally {
        Remove-Item $p -Force -ErrorAction SilentlyContinue
    }
}

function Run-Fps([string]$text) {
    $p = [IO.Path]::GetTempFileName() + '.wen'
    [IO.File]::WriteAllText($p, $text, (New-Object Text.UTF8Encoding($false)))
    try {
        $out = & $exe $p 2>$null
        $last = ($out | Select-Object -Last 1)
        return [double]$last
    } finally {
        Remove-Item $p -Force -ErrorAction SilentlyContinue
    }
}
$arith = @'
将 i 变成 0
将 和 变成 0
当 i 小于 1000000 则
将 和 变成 和 + i * 2
将 i 变成 i + 1
结束
打印 和
'@
$ray = @'
依赖 视口库, 时间库
将 起 变成 高精度时间()
将 i 变成 0
当 i 小于 20 则
将 图 变成 光追渲染(160, 90, 1)
将 i 变成 i + 1
结束
将 秒 变成 高精度时间() - 起
打印 20 / 秒
'@
$fn = @'
函数 加一 x 则
返回 x + 1
结束
将 i 变成 0
将 和 变成 0
当 i 小于 1000000 则
将 和 变成 和 + 加一(i)
将 i 变成 i + 1
结束
打印 和
'@
$ray = @'
依赖 视口库, 时间库
将 起 变成 高精度时间()
将 i 变成 0
当 i 小于 20 则
将 图 变成 光追渲染(160, 90, 1)
将 i 变成 i + 1
结束
将 秒 变成 高精度时间() - 起
打印 20 / 秒
'@
$fac = @'
依赖 大整数库
打印 长度 大整数阶乘(100000)
'@
$pi = @'
依赖 数学库
打印 长度 高精度圆周率(10000)
'@
$lib = @'
依赖 数学库
将 i 变成 0
将 和 变成 0
当 i 小于 1000000 则
将 和 变成 和 + 平方根(i % 100 + 1) + 长度("abcdef")
将 i 变成 i + 1
结束
打印 和
'@
$ray = @'
依赖 视口库, 时间库
将 起 变成 高精度时间()
将 i 变成 0
当 i 小于 20 则
将 图 变成 光追渲染(160, 90, 1)
将 i 变成 i + 1
结束
将 秒 变成 高精度时间() - 起
打印 20 / 秒
'@

$ta = Run-Script $arith
$tf = Run-Script $fn
$tp = Run-Script $pi
$tl = Run-Script $lib
$rayFps = [Math]::Max((Run-Fps $ray), (Run-Fps $ray))
$t1 = Run-Script $fac
$t2 = Run-Script $fac
$tbig = [Math]::Min($t1, $t2)

$arithUs = $ta * 1000000.0 / 1000000.0
$fnUs = $tf * 1000000.0 / 1000000.0
$libUs = $tl * 1000000.0 / 1000000.0
$bigOk = $tbig -lt 0.5
$rayTarget = if ($32) { 100 } else { 240 }
$rayOk = $rayFps -ge $rayTarget
$calcOk = ($arithUs -lt 35) -and ($fnUs -lt 35) -and ($libUs -lt 35) -and ($tp -lt 0.5)

[pscustomobject]@{
    架构 = if ($32) { '32 位' } else { '64 位' }
    算术微秒 = [Math]::Round($arithUs, 3)
    函数微秒 = [Math]::Round($fnUs, 3)
    库调用微秒 = [Math]::Round($libUs, 3)
    阶乘秒 = [Math]::Round($tbig, 3)
    圆周率秒 = [Math]::Round($tp, 3)
    光追帧 = [Math]::Round($rayFps, 1)
    单次达标 = $calcOk
    庞大达标 = $bigOk
    光追达标 = $rayOk
} | Format-Table -AutoSize

if (-not ($calcOk -and $bigOk -and $rayOk)) { exit 1 }
