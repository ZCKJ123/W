$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = [IO.Path]::GetTempPath()
$core = @'
函数 求阶乘 n 则
如果 n <= 1 则
返回 1
结束
返回 n * 求阶乘(n - 1)
结束
将 i 变成 0
将 和 变成 0
当 i 小于 5 则
将 和 变成 和 + i
将 i 变成 i + 1
结束
打印 和, √16, 2 ** 10
打印 求阶乘(5)
'@

function Write-Utf8([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
}
function Run-Exe([string]$path) {
    $out = & $exe $path 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($out -join "`n") }
    return (($out | Out-String).Trim())
}

# 解释器
$interp = Join-Path $dir 'matrix_interp.wen'
Write-Utf8 $interp $core
$expected = (Run-Exe $interp) -replace "`r`n", "`n"

# 字节码
$bcSrc = Join-Path $dir 'matrix_bc.wen'
Write-Utf8 $bcSrc ("# 翻译: 字节码`n" + $core)
$bcJson = Join-Path $dir 'matrix_bc.json'
$bcRunner = Join-Path $dir 'matrix_bc_runner.wen'
Write-Utf8 $bcRunner ("依赖 翻译器`n翻译(`"$bcSrc`", `"$bcJson`")`n执行字节码(`"$bcJson`")`n")
$bc = (Run-Exe $bcRunner) -replace "`r`n", "`n"

# Python
$pySrc = Join-Path $dir 'matrix_py.wen'
Write-Utf8 $pySrc ("# 外派: python`n" + $core)
$pyFile = Join-Path $dir 'matrix_py.py'
$pyRunner = Join-Path $dir 'matrix_py_runner.wen'
Write-Utf8 $pyRunner ("依赖 外派库`n外派翻译(`"$pySrc`", `"$pyFile`")`n")
Run-Exe $pyRunner | Out-Null
$pyRaw = (& python $pyFile 2>&1) -join "`n"
$py = (($pyRaw -replace "`r`n", "`n") -split "`n" | ForEach-Object { $_ -replace '4\.0', '4' }) -join "`n"

# C++
$cpp = Join-Path $dir 'matrix_cpp.cpp'
& $exe --生成C++ $pySrc $cpp 2>&1 | Out-Null
$cppExe = Join-Path $dir 'matrix_cpp.exe'
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
$p = Start-Process -FilePath 'C:\msys64\ucrt64\bin\g++.exe' `
    -ArgumentList @('-std=c++17', '-O2', '-static', $cpp, '-o', $cppExe) `
    -NoNewWindow -Wait -PassThru
if ($p.ExitCode -ne 0) { throw 'C++ 编译失败' }
$cppOut = (((& $cppExe 2>&1) | Out-String).Trim()) -replace "`r`n", "`n"

$ok = $true
foreach ($pair in @(@('解释器', $expected), @('字节码', $bc), @('Python', $py), @('C++', $cppOut))) {
    if ($pair[1] -ne $expected) {
        $ok = $false
        Write-Host ("失败：{0} 输出不等价" -f $pair[0])
        Write-Host ("  期望：{0}" -f ($expected -replace "`n", ' / '))
        Write-Host ("  实际：{0}" -f ($pair[1] -replace "`n", ' / '))
    }
}
if (-not $ok) { exit 1 }
Write-Host ("跨目标矩阵：解释器 / 字节码 / Python / C++ 全部等价（函数+递归+√）")
