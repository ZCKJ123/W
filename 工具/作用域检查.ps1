# 作用域检查：找出“函数里给全局同名变量赋值”的地方
# 语言规范：函数体第一次赋值的名字是局部变量；但如果外层已有同名变量，
# 再赋值改的是外层——库作者最容易被这条坑到。
# 用法：powershell -File 工具\作用域检查.ps1 [-Path 库]

param([string]$Path = '库')

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

$files = Get-ChildItem $Path -Filter *.wen -File
$total = 0
$report = @()

foreach ($f in $files) {
    $lines = [IO.File]::ReadAllLines($f.FullName, [Text.UTF8Encoding]::new($false))
    $globals = @{}
    $inFunc = $false
    $funcName = ''
    $params = @{}
    $issues = @()

    foreach ($raw in $lines) {
        $trim = $raw.Trim()
        if ($trim -eq '' -or $trim.StartsWith('#')) { continue }

        if (-not $inFunc) {
            if ($raw -match '^函数\s+(\S+)\s*(.*?)则\s*$') {
                $inFunc = $true
                $funcName = $matches[1]
                $params = @{}
                foreach ($p in ($matches[2].Split(' '))) {
                    if ($p -ne '') { $params[$p] = $true }
                }
                continue
            }
            if ($raw -match '^将\s+(\S+)\s+变成') { $globals[$matches[1]] = $true }
            continue
        }

        if ($raw -match '^结束\s*$') {
            $inFunc = $false
            continue
        }
        if ($trim -match '^将\s+(\S+)\s+变成\s') {
            $name = $matches[1]
            if ($globals.ContainsKey($name) -and -not $params.ContainsKey($name)) {
                $issues += ($funcName + '/' + $name)
            }
        }
    }

    if ($issues.Count -gt 0) {
        $total += $issues.Count
        $report += [pscustomobject]@{ 文件 = $f.Name; 处数 = $issues.Count; 明细 = ($issues -join '，') }
    }
}

if ($report.Count -eq 0) {
    Write-Output '没有发现“函数内改全局同名变量”的写法。'
    exit 0
}

Write-Output ('共 ' + $total + ' 处：函数里赋值的名字在顶层也有同名变量，会改到全局。')
$report | Sort-Object -Property 处数 -Descending | Format-Table -AutoSize | Out-String -Width 220
exit 1
