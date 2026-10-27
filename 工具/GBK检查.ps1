$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$tmp = [IO.Path]::GetTempFileName() + '.wen'
$text = "打印 `"你好，世界`"`n"
[IO.File]::WriteAllBytes($tmp, [Text.Encoding]::GetEncoding(936).GetBytes($text))
try {
    $check = & $exe --检查 $tmp 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "GBK 检查失败：$check"
        exit 1
    }
    $out = (& $exe $tmp 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $out -notmatch '你好，世界') {
        Write-Host "GBK 运行失败：$out"
        exit 1
    }
    Write-Host "GBK 源：通过"
} finally {
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
}
