param(
    [switch]$备份,
    [switch]$检查,
    [switch]$恢复,
    [switch]$确认,
    [string]$快照 = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$backupRoot = Join-Path $root '隔离备份'
$coreFiles = @(
    'wen.cpp',
    '语法.ebnf',
    '使用规范.md',
    '语言规范.md',
    '语法总表.md',
    '0.2\语法公理.md'
)
$coreDirs = @('测试', '工程', '工具', '插件')

function Get-FileHashSafe([string]$path) {
    if (-not (Test-Path $path)) { return '' }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
}

function Get-Snapshot {
    $files = @()
    foreach ($f in $coreFiles) {
        $p = Join-Path $root $f
        if (Test-Path $p) {
            $files += [pscustomobject]@{ 路径 = $f; 哈希 = (Get-FileHashSafe $p); 大小 = (Get-Item $p).Length }
        }
    }
    foreach ($d in $coreDirs) {
        $dp = Join-Path $root $d
        if (-not (Test-Path $dp)) { continue }
        Get-ChildItem $dp -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($root.Length + 1)
            $files += [pscustomobject]@{ 路径 = $rel; 哈希 = (Get-FileHashSafe $_.FullName); 大小 = $_.Length }
        }
    }
    return $files
}

function LatestSnapshot {
    if (-not (Test-Path $backupRoot)) { return $null }
    return Get-ChildItem $backupRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
}

if ($备份) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $dest = Join-Path $backupRoot $stamp
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($f in $coreFiles) {
        $src = Join-Path $root $f
        if (-not (Test-Path $src)) { continue }
        $to = Join-Path $dest $f
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item $src $to -Force
    }
    foreach ($d in $coreDirs) {
        $src = Join-Path $root $d
        if (-not (Test-Path $src)) { continue }
        $to = Join-Path $dest $d
        New-Item -ItemType Directory -Force $to | Out-Null
        Copy-Item (Join-Path $src '*') $to -Recurse -Force
    }
    $files = Get-Snapshot
    $manifest = [pscustomobject]@{
        时间 = (Get-Date).ToString('o')
        文件 = $files
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $dest 'manifest.json') -Encoding UTF8
    Write-Host "已创建隔离快照：$dest"
    Write-Host ("文件数：{0}" -f $files.Count)
    exit 0
}

if ($检查) {
    $latest = LatestSnapshot
    if (-not $latest) { Write-Host "还没有隔离快照，先运行 -备份"; exit 1 }
    $manifest = Get-Content (Join-Path $latest.FullName 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $old = @{}
    foreach ($x in $manifest.文件) { $old[$x.路径] = $x.哈希 }
    $now = @{}
    foreach ($x in Get-Snapshot) { $now[$x.路径] = $x.哈希 }
    $changed = @(); $added = @(); $deleted = @()
    foreach ($k in $now.Keys) {
        if (-not $old.ContainsKey($k)) { $added += $k }
        elseif ($old[$k] -ne $now[$k]) { $changed += $k }
    }
    foreach ($k in $old.Keys) { if (-not $now.ContainsKey($k)) { $deleted += $k } }
    Write-Host ("基准：{0}" -f $latest.Name)
    Write-Host ("变更：{0}  新增：{1}  删除：{2}" -f $changed.Count, $added.Count, $deleted.Count)
    foreach ($k in ($changed | Sort-Object)) { Write-Host ("  M {0}" -f $k) }
    foreach ($k in ($added | Sort-Object)) { Write-Host ("  + {0}" -f $k) }
    foreach ($k in ($deleted | Sort-Object)) { Write-Host ("  - {0}" -f $k) }
    if ($changed.Count -or $added.Count -or $deleted.Count) { exit 2 }
    Write-Host "核心与测试未偏离快照。"
    exit 0
}

if ($恢复) {
    if (-not $确认) { Write-Host "恢复会覆盖当前源码；确认请加 -确认"; exit 1 }
    $latest = if ($快照) { Get-Item (Join-Path $backupRoot $快照) } else { LatestSnapshot }
    if (-not $latest -or -not (Test-Path $latest.FullName)) { Write-Host "找不到快照"; exit 1 }
    foreach ($f in $coreFiles) {
        $src = Join-Path $latest.FullName $f
        if (-not (Test-Path $src)) { continue }
        Copy-Item $src (Join-Path $root $f) -Force
        Write-Host "恢复 $f"
    }
    foreach ($d in $coreDirs) {
        $src = Join-Path $latest.FullName $d
        if (-not (Test-Path $src)) { continue }
        Copy-Item (Join-Path $src '*') (Join-Path $root $d) -Recurse -Force
        Write-Host "恢复 $d"
    }
    Write-Host "恢复完成。重新构建：.\构建.bat"
    exit 0
}

Write-Host "用法："
Write-Host "  powershell -File 工具\隔离.ps1 -备份"
Write-Host "  powershell -File 工具\隔离.ps1 -检查"
Write-Host "  powershell -File 工具\隔离.ps1 -恢复 -确认"
