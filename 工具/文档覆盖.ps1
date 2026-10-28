param([switch]$严格)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$docs = @('使用规范.md', '使用教程.md', '语法总表.md', '语言规范.md')
$lines = & $exe --库 2>&1
$names = New-Object System.Collections.Generic.HashSet[string]
foreach ($line in $lines) {
    $s = ($line -replace "`r", '').Trim()
    if (-not $s) { continue }
    if ($s -match '^核心') { continue }
    if ($s -match '库（\d+ 个函数）') { continue }
    foreach ($part in ($s -split '、')) {
        $n = $part.Trim()
        if ($n -and $n -notmatch '^核心' -and $n.Length -le 20) { [void]$names.Add($n) }
    }
}
$bodies = @{}
foreach ($d in $docs) {
    $p = Join-Path $root $d
    $bodies[$d] = if (Test-Path $p) { [IO.File]::ReadAllText($p, [Text.Encoding]::UTF8) } else { '' }
}
$missing = @()
foreach ($n in $names) {
    $found = $false
    foreach ($d in $docs) {
        if ($bodies[$d].Contains($n)) { $found = $true; break }
    }
    if (-not $found) { $missing += $n }
}
Write-Host ("库函数：{0} 个；文档完全未提及：{1} 个" -f $names.Count, $missing.Count)
if ($missing.Count) {
    Write-Host ("未提及：" + (($missing | Sort-Object | Select-Object -First 40) -join '、'))
    if ($missing.Count -gt 40) { Write-Host ("……还有 {0} 个" -f ($missing.Count - 40)) }
}
if ($严格 -and $missing.Count -gt 0) { exit 1 }
exit 0
