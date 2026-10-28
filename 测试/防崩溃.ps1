$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = Join-Path ([IO.Path]::GetTempPath()) ('wen_crash_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null
$fail = 0

try {
    $big = 'x' * 1200000
    $mf = Join-Path $dir 'mem.wen'
    [IO.File]::WriteAllText($mf, "依赖 内存库`n内存上限(1)`n内存存(`"大`", `"$big`")`n", (New-Object Text.UTF8Encoding($false)))
    $mout = (& $exe $mf 2>&1) -join "`n"
    $mcode = $LASTEXITCODE
    if ($mcode -ne 1 -or $mout -notmatch '内存超过上限') {
        $fail++
        Write-Host ("失败：内存超限没有干净报错，exit={0}" -f $mcode)
    } else { Write-Host '通过：内存超限主动报错' }

    $rf = Join-Path $dir 'rec.wen'
    [IO.File]::WriteAllText($rf, "函数 递归 则`n递归()`n结束`n递归()`n", (New-Object Text.UTF8Encoding($false)))
    $rout = (& $exe $rf 2>&1) -join "`n"
    $rcode = $LASTEXITCODE
    if ($rcode -ne 1 -or $rout -notmatch '递归过深') {
        $fail++
        Write-Host ("失败：递归极限没有受控，exit={0}" -f $rcode)
    } else { Write-Host '通过：递归极限受控' }

    $tf = Join-Path $dir 'time.wen'
    [IO.File]::WriteAllText($tf, "当 真 则`n结束`n", (New-Object Text.UTF8Encoding($false)))
    $to = Join-Path $dir 't.out'; $te = Join-Path $dir 't.err'
    $p = Start-Process -FilePath $exe -ArgumentList @('--限制', '时间=1', $tf) -WorkingDirectory $root `
        -RedirectStandardOutput $to -RedirectStandardError $te -NoNewWindow -PassThru
    Start-Sleep -Seconds 3
    $terr = [IO.File]::ReadAllText($te, [Text.Encoding]::UTF8)
    if ($p -and -not $p.HasExited) { $p.Kill() }
    if ($terr -notmatch '资源限制') {
        $fail++
        Write-Host "失败：时间限制没有主动终止"
    } else { Write-Host '通过：时间限制主动终止' }
} catch {
    $fail++
    Write-Host "失败：防崩溃测试异常：$_"
} finally {
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail -gt 0) { exit 1 }
Write-Host '防崩溃回归：内存/递归/时间上限通过'
exit 0
