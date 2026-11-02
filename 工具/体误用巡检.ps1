$ErrorActionPreference = 'Continue'
$exe = Join-Path $PSScriptRoot '..\wen.exe'
$dir = Join-Path $env:TEMP '体误用'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$prefix = "依赖 四维库`r`n将 彩 变成 体新建彩色(4, 4, 4)`r`n将 灰 变成 体新建(4, 4, 4)`r`n将 四 变成 四维球体(3, 4, 4, 4, 1)`r`n"
$cases = @(
    '体总和(5)',
    '体彩写(灰, 1, 1, 1, 1, 2, 3)',
    '体读(四, 1, 1, 1)',
    '体切片(彩, 99)',
    '体新建(0, 4, 4)',
    '体形态(彩, -1, 1)',
    '体布尔(彩, 灰, 0)',
    '体采样(四, 1, 1, 1)',
    '四维采样(彩, 1, 1, 1, 1)',
    '体缩放(彩, -5, 4, 4)',
    '体写(彩, 99, 1, 1, 5)',
    '体差分(彩, 灰)',
    '体转缓冲(1)',
    '缓冲转体("abc")',
    '体尺寸("x")',
    '体渲染(彩, 0, 0)',
    '体等值面(灰, 1)'
)
$crash = 0
foreach ($c in $cases) {
    $src = $prefix + $c + "`r`n"
    $f = Join-Path $dir '误用.wen'
    [IO.File]::WriteAllText($f, $src, (New-Object Text.UTF8Encoding($false)))
    $out = & $exe $f 2>&1 | Out-String
    $code = $LASTEXITCODE
    $first = ($out -split "`r?`n" | Where-Object { $_ -match '原因：' } | Select-Object -First 1)
    if ($code -lt 0 -or $code -gt 1000) { $crash++; Write-Host ("崩溃：" + $c + " （" + $code + "）") }
    elseif ($code -ne 0 -and $out -notmatch '【(运行时|语法|静态)错误】') { $crash++; Write-Host ("无消息失败：" + $c) }
    else { Write-Host ("ok  " + $c + "  →  " + ($first -replace '.*原因：', '')) }
}
if ($crash -gt 0) { Write-Host ("误用巡检：{0} 项崩溃" -f $crash); exit 1 }
Write-Host ("误用巡检：{0} 项，全部给出错误信息、无崩溃" -f $cases.Count)
exit 0
