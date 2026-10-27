$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$tmp = [IO.Path]::GetTempFileName() + '.wen'
$data = [IO.Path]::GetTempFileName() + '.txt'
$runner = [IO.Path]::GetTempFileName() + '.wen'
$text = "打印 `"你好，世界`"`n"
[IO.File]::WriteAllBytes($tmp, [Text.Encoding]::GetEncoding(936).GetBytes($text))
[IO.File]::WriteAllBytes($data, [Text.Encoding]::GetEncoding(936).GetBytes("甲,乙`n"))
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
    $path = $data.Replace('\', '/')
    [IO.File]::WriteAllText($runner, "依赖 文件库`n打印 读取(`"$path`")`n",
        (New-Object Text.UTF8Encoding($false)))
    $dataOut = (& $exe $runner 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $dataOut -notmatch '甲,乙') {
        Write-Host "GBK 数据读取失败：$dataOut"
        exit 1
    }
    Write-Host "GBK 源 / 数据：通过"
} finally {
    Remove-Item $tmp, $data, $runner -Force -ErrorAction SilentlyContinue
}
