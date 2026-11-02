$ErrorActionPreference = 'Continue'
# 体尺寸巡检：极小的、奇形的体积最容易越界（1×1×1、1×N×N、3×5×7 之类）
# 每种尺寸跑一批算子，只要求「不崩、有明确结果或明确报错」
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = Join-Path $env:TEMP '体尺寸巡检'
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$dims = @(
    @{名 = '1x1x1'; l = 1; r = 1; c = 1},
    @{名 = '1x1x9'; l = 1; r = 1; c = 9},
    @{名 = '1x9x9'; l = 1; r = 9; c = 9},
    @{名 = '9x1x1'; l = 9; r = 1; c = 1},
    @{名 = '2x2x2'; l = 2; r = 2; c = 2},
    @{名 = '3x5x7'; l = 3; r = 5; c = 7}
)

$ops = @(
    '体写(体, 0, 0, 0, 200)',
    '打印 体读(体, 0, 0, 0)',
    '打印 缓冲长度(体切片(体, 0))',
    '打印 体总和(体)',
    '打印 体极值(体)',
    '打印 体质心(体)',
    '打印 体表面积(体)',
    '打印 体表面格(体)',
    '将 出 变成 体平滑(体)',
    '将 出 变成 体梯度(体)',
    '将 出 变成 体模糊(体, 1)',
    '将 出 变成 体形态(体, 1, 1)',
    '将 出 变成 体形态(体, 1, 0)',
    '将 出 变成 体旋转(体, 0, 90)',
    '将 出 变成 体旋转(体, 1, 45)',
    '将 出 变成 体缩放(体, 1, 1, 1)',
    '将 出 变成 体缩放(体, 5, 5, 5)',
    '将 出 变成 体差分(体, 体)',
    '将 出 变成 体布尔(体, 体, 0)',
    '打印 缓冲长度(体沿轴(体, 0, 0))',
    '打印 缓冲长度(体渲染(体, 4, 4))',
    '打印 体采样(体, 0, 0, 0)',
    '打印 体球(体, 0, 0, 0, 1, 200, 100, 50)',
    '打印 体盒(体, 0, 0, 0, 9, 9, 9, 60, 60, 60)',
    '将 出 变成 体等值面(体, 1)',
    '将 出 变成 缓冲转体(体转缓冲(体))',
    '打印 体释放(体)'
)

$fail = 0
$checked = 0
$crash = 0
foreach ($d in $dims) {
    foreach ($op in $ops) {
        $checked++
        $src = ("依赖 体库`r`n将 体 变成 体新建(" + $d.l + ", " + $d.r + ", " + $d.c + ")`r`n" + $op + "`r`n")
        $f = Join-Path $dir '尺寸.wen'
        [IO.File]::WriteAllText($f, $src, (New-Object Text.UTF8Encoding($false)))
        $out = & $exe $f 2>&1 | Out-String
        $code = $LASTEXITCODE
        if ($code -ne 0) {
            if ($code -lt 0 -or $code -gt 1000) {
                $fail++; $crash++
                Write-Host ("崩溃：" + $d.名 + "  " + $op + "  （" + $code + "）")
            } elseif ($out -notmatch '【(运行时|静态|语法|编译|文件)错误】') {
                $fail++
                Write-Host ("失败：" + $d.名 + "  " + $op + "  （" + $code + "）")
            }
        }
    }
}
if ($fail -gt 0) { Write-Host ("体尺寸巡检：{0} 项有问题（其中崩溃 {1}）" -f $fail, $crash); exit 1 }
Write-Host ("体尺寸巡检：{0} 种尺寸 × {1} 个算子，共 {2} 项，无崩溃" -f $dims.Count, $ops.Count, $checked)
exit 0
