# 运行.ps1 —— 「文」语言自动测试
# 用法：
#   .\测试\运行.ps1            只跑测试
#   .\测试\运行.ps1 -Build     先重新构建 wen.exe 再跑
#
# 用例：测试\用例\*.wen，期望输出在同名 .期望；
# 错误：测试\错误\*.wen，期望标准错误包含同名 .错误 里的文字。

param([switch]$Build)

$ErrorActionPreference = 'Stop'
$env:WEN_ADMIN_TOKEN = 'wen-test'
$env:WEN_SECURITY_TOKEN = 'wen-security'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'

if ($Build) {
    Write-Host '== 构建 =='
    & (Join-Path $root '构建.bat')
    if ($LASTEXITCODE -ne 0) { throw '构建失败' }
}

if (-not (Test-Path $exe)) {
    throw "找不到 wen.exe：$exe（先运行 构建.bat）"
}

function Normalize([string]$text) {
    return (($text -replace "`r`n", "`n").TrimEnd())
}

function Run-Wen([string[]]$arguments) {
    $outFile = [IO.Path]::GetTempFileName()
    $errFile = [IO.Path]::GetTempFileName()
    try {
        $quoted = @()
        foreach ($a in $arguments) { $quoted += ('"' + $a + '"') }
        $p = Start-Process -FilePath $exe -ArgumentList $quoted `
            -WorkingDirectory $root `
            -RedirectStandardOutput $outFile -RedirectStandardError $errFile `
            -NoNewWindow -Wait -PassThru
        return @{
            Code   = $p.ExitCode
            Output = [IO.File]::ReadAllText($outFile, [Text.Encoding]::UTF8)
            Error  = [IO.File]::ReadAllText($errFile, [Text.Encoding]::UTF8)
        }
    } finally {
        Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    }
}

function Show-Diff([string]$expected, [string]$actual) {
    $e = Normalize $expected
    $a = Normalize $actual
    $el = $e -split "`n"
    $al = $a -split "`n"
    $n = [Math]::Max($el.Count, $al.Count)
    $shown = 0
    for ($i = 0; $i -lt $n -and $shown -lt 5; $i++) {
        $ev = if ($i -lt $el.Count) { $el[$i] } else { '<无>' }
        $av = if ($i -lt $al.Count) { $al[$i] } else { '<无>' }
        if ($ev -ne $av) {
            Write-Host ("    第 {0} 行" -f ($i + 1))
            Write-Host ("      期望：{0}" -f $ev)
            Write-Host ("      实际：{0}" -f $av)
            $shown++
        }
    }
}

$fail = 0
$total = 0

# ---- 例子语法检查 ----
Write-Host '== 例子语法检查 =='
$exampleDir = Join-Path $root '例子'
$exampleFail = 0
$examples = Get-ChildItem $exampleDir -Filter *.wen | Sort-Object Name
foreach ($f in $examples) {
    $total++
    $r = Run-Wen @('--检查', $f.FullName)
    if ($r.Code -ne 0) {
        Write-Host ("失败：{0} --检查" -f $f.Name)
        if ($r.Error) { Write-Host ($r.Error.Trim()) }
        $fail++
        $exampleFail++
    }
}
if ($exampleFail -eq 0) { Write-Host ("通过：{0} 个示例" -f $examples.Count) }

# ---- 正确用例 ----
Write-Host "`n== 用例 =="
$caseDir = Join-Path $PSScriptRoot '用例'
foreach ($f in (Get-ChildItem $caseDir -Filter *.wen | Sort-Object Name)) {
    $total++
    $expectedPath = [IO.Path]::ChangeExtension($f.FullName, '.期望')
    if (-not (Test-Path $expectedPath)) {
        Write-Host ("失败：{0} 缺少期望输出文件" -f $f.Name)
        $fail++
        continue
    }
    $r = Run-Wen @($f.FullName)
    $expected = [IO.File]::ReadAllText($expectedPath, [Text.Encoding]::UTF8)
    if ($r.Code -ne 0) {
        Write-Host ("失败：{0} 退出码 {1}" -f $f.Name, $r.Code)
        if ($r.Error) { Write-Host ($r.Error.Trim()) }
        $fail++
    } elseif ((Normalize $r.Output) -ne (Normalize $expected)) {
        Write-Host ("失败：{0} 输出不一致" -f $f.Name)
        Show-Diff $expected $r.Output
        $fail++
    } else {
        Write-Host ("通过：{0}" -f $f.Name)
    }
}

# ---- 错误用例 ----
Write-Host "`n== 错误用例 =="
$errorDir = Join-Path $PSScriptRoot '错误'
if (Test-Path $errorDir) {
    foreach ($f in (Get-ChildItem $errorDir -Filter *.wen | Sort-Object Name)) {
        $total++
        $expectedPath = [IO.Path]::ChangeExtension($f.FullName, '.错误')
        if (-not (Test-Path $expectedPath)) {
            Write-Host ("失败：{0} 缺少期望错误文件" -f $f.Name)
            $fail++
            continue
        }
        $expected = ([IO.File]::ReadAllText($expectedPath, [Text.Encoding]::UTF8)).Trim()
        $r = Run-Wen @($f.FullName)
        if ($r.Code -eq 0) {
            Write-Host ("失败：{0} 本应失败，却成功了" -f $f.Name)
            $fail++
        } elseif ($r.Error -notmatch [regex]::Escape($expected)) {
            Write-Host ("失败：{0} 错误信息不含「{1}」" -f $f.Name, $expected)
            if ($r.Error) { Write-Host ($r.Error.Trim()) }
            $fail++
        } else {
            Write-Host ("通过：{0}" -f $f.Name)
        }
    }
}

Write-Host ("`n通过 {0}/{1}" -f ($total - $fail), $total)
if ($fail -gt 0) { exit 1 }
& (Join-Path $PSScriptRoot '错误协议.ps1')
if ($LASTEXITCODE -ne 0) { exit 1 }
& (Join-Path $PSScriptRoot '防崩溃.ps1')
if ($LASTEXITCODE -ne 0) { exit 1 }
& (Join-Path $PSScriptRoot '..\工具\索引检查.ps1')
if ($LASTEXITCODE -ne 0) { exit 1 }
& (Join-Path $PSScriptRoot '..\工具\自述检查.ps1')
if ($LASTEXITCODE -ne 0) { exit 1 }
exit 0
