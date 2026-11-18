param([switch]$Quick, [switch]$扫描)
# 光追（光影）基准：全部走 CPU 路径追踪内置 `光追渲染`，只测光追本身与
# 语言侧每帧的必要开销（叠加画线、存 BMP），不改场景内容。
# 用法：
#   .\工具\光追基准.ps1
#   .\工具\光追基准.ps1 -Quick    # 只测实时档与 160x90
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'

if (-not (Test-Path -LiteralPath $exe)) { throw "找不到 $exe" }

function 跑帧率([string]$脚本) {
    $p = Join-Path $env:TEMP ('wen_raybench_' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.wen')
    [IO.File]::WriteAllText($p, $脚本, [Text.UTF8Encoding]::new($false))
    try {
        $out = & $exe $p 2>$null
        $text = ($out | Select-Object -Last 1)
        $parts = "$text".Trim() -split ' '
        return [pscustomobject]@{ 帧率 = [double]$parts[0]; 毫秒 = [double]$parts[1] }
    } finally {
        Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue
    }
}

function 造脚本([int]$宽, [int]$高, [int]$采样, [double]$缩放, [int]$帧, [string]$附言, [string]$叠加) {
    $body = @"
依赖 视口库, 时间库
将 起 变成 高精度时间()
将 帧 变成 0
当 帧 小于 $帧 则
    将 图 变成 光追渲染($宽, $高, $采样, 0, $缩放)
$叠加
    将 帧 变成 帧 + 1
结束
将 秒 变成 高精度时间() - 起
打印 文本($帧 / 秒) + " " + 文本(秒 / $帧 * 1000)
"@
    return $body
}

$档位 = @(
    @{ 名 = '640x360 原生 / 采样1'; 宽 = 640; 高 = 360; 采样 = 1; 缩放 = 1.0; 帧 = 8 },
    @{ 名 = '640x360 内部 0.25 实时档 / 采样1'; 宽 = 640; 高 = 360; 采样 = 1; 缩放 = 0.25; 帧 = 20 },
    @{ 名 = '320x180 / 采样1'; 宽 = 320; 高 = 180; 采样 = 1; 缩放 = 1.0; 帧 = 20 },
    @{ 名 = '320x180 内部 0.5 / 采样1'; 宽 = 320; 高 = 180; 采样 = 1; 缩放 = 0.5; 帧 = 20 },
    @{ 名 = '320x180 / 采样4'; 宽 = 320; 高 = 180; 采样 = 4; 缩放 = 1.0; 帧 = 8 },
    @{ 名 = '160x90 / 采样1'; 宽 = 160; 高 = 90; 采样 = 1; 缩放 = 1.0; 帧 = 40 }
)
if ($Quick) { $档位 = $档位 | Where-Object { $_.名 -match '0\.25|160x90' } }

$结果 = @()
foreach ($档 in $档位) {
    $r = 跑帧率 (造脚本 $档.宽 $档.高 $档.采样 $档.缩放 $档.帧 $档.名 '')
    $结果 += [pscustomobject]@{
        档位 = $档.名
        帧率 = [Math]::Round($r.帧率, 1)
        单帧毫秒 = [Math]::Round($r.毫秒, 2)
    }
}

# -扫描：640x360 输出、内部缩放从 0.20 往上加，找 165 帧还能守住的最大清晰度。
# 165 是 64 位门禁目标；清晰度按内部像素数算（缩放 0.35 即 224x126）。
if ($扫描) {
    $扫 = @()
    $档表 = @(
        @{ 缩放 = 0.20; 采样 = 1 }, @{ 缩放 = 0.24; 采样 = 1 }, @{ 缩放 = 0.28; 采样 = 1 },
        @{ 缩放 = 0.30; 采样 = 1 }, @{ 缩放 = 0.32; 采样 = 1 }, @{ 缩放 = 0.34; 采样 = 1 },
        @{ 缩放 = 0.36; 采样 = 1 }, @{ 缩放 = 0.40; 采样 = 1 }, @{ 缩放 = 0.24; 采样 = 2 },
        @{ 缩放 = 0.28; 采样 = 2 }, @{ 缩放 = 0.32; 采样 = 2 }, @{ 缩放 = 0.24; 采样 = 4 }
    )
    foreach ($档 in $档表) {
        $r = 跑帧率 (造脚本 640 360 $档.采样 $档.缩放 20 '')
        $内宽 = [int][Math]::Round(640 * $档.缩放)
        $内高 = [int][Math]::Round(360 * $档.缩放)
        $扫 += [pscustomobject]@{
            缩放 = $档.缩放
            采样 = $档.采样
            内部像素 = "$内宽×$内高"
            帧率 = [Math]::Round($r.帧率, 1)
            单帧毫秒 = [Math]::Round($r.毫秒, 2)
            过165 = $r.帧率 -ge 165
        }
    }
    Write-Output ''
    Write-Output '内部缩放扫描（640×360 输出，采样 1 / 2 / 4）'
    $扫 | Format-Table -AutoSize
}

# 语言侧开销：每帧叠加两条画线、每帧存一次 BMP，看各吃掉多少帧
$叠加档 = @(
    @{ 名 = '实时档 + 每帧 2 条画线'; 宽 = 640; 高 = 360; 采样 = 1; 缩放 = 0.25; 帧 = 20; 叠加 = '    将 图 变成 视口画线(图, 0, 0, 640, 0, 颜色(255, 255, 255))' + "`r`n" + '    将 图 变成 视口画线(图, 0, 359, 640, 359, 颜色(255, 255, 255))' },
    @{ 名 = '实时档 + 每帧存 BMP'; 宽 = 640; 高 = 360; 采样 = 1; 缩放 = 0.25; 帧 = 20; 叠加 = '    视口保存(图, "C:/Temp/光追基准_每帧.bmp")' }
)
foreach ($档 in $叠加档) {
    $r = 跑帧率 (造脚本 $档.宽 $档.高 $档.采样 $档.缩放 $档.帧 $档.名 $档.叠加)
    $结果 += [pscustomobject]@{
        档位 = $档.名
        帧率 = [Math]::Round($r.帧率, 1)
        单帧毫秒 = [Math]::Round($r.毫秒, 2)
    }
}

$结果 | Format-Table -AutoSize

# 定位：逐像素自绘一张 640x360 需要多久（语言侧不能拿它当光追渲染用）
$逐像素 = @'
依赖 视口库, 时间库
将 图 变成 新建视口(640, 360)
将 起 变成 高精度时间()
将 y 变成 0
当 y 小于 360 则
    将 x 变成 0
    当 x 小于 640 则
        将 图 变成 视口画点(图, x, y, 颜色(255, 128, 0))
        将 x 变成 x + 1
    结束
    将 y 变成 y + 1
结束
将 秒 变成 高精度时间() - 起
打印 文本(秒 * 1000) + " " + 文本(230400 / 秒)
'@
$p = Join-Path $env:TEMP ('wen_raypix_' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.wen')
[IO.File]::WriteAllText($p, $逐像素, [Text.UTF8Encoding]::new($false))
try {
    $out = & $exe $p 2>$null
    $parts = "$($out | Select-Object -Last 1)".Trim() -split ' '
    Write-Output ("逐像素自绘 640x360：{0} ms，折合 {1} 点/秒，按此上限约 {2} 帧/秒" -f `
        [Math]::Round([double]$parts[0], 1), [Math]::Round([double]$parts[1], 0), [Math]::Round([double]$parts[1] / 230400, 1))
} finally {
    Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath 'C:\Temp\光追基准_每帧.bmp' -ErrorAction SilentlyContinue
}
exit 0
