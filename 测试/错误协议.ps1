$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = Join-Path ([IO.Path]::GetTempPath()) ('wen_proto_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null
$fail = 0

function Invoke-Wen([string[]]$arguments) {
    $out = Join-Path $dir 'out.txt'
    $err = Join-Path $dir 'err.txt'
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root `
        -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow -Wait -PassThru
    return @{
        code = $p.ExitCode
        out  = [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8)
        err  = [IO.File]::ReadAllText($err, [Text.Encoding]::UTF8)
    }
}

try {
    $cases = @(
        @{ n = 'newline'; t = '打印 1 打印 2'; r = 'syntax.need_newline'; rt = $false },
        @{ n = 'undefined'; t = '打印 不存在'; r = 'static.undefined_name'; rt = $false },
        @{ n = 'arity'; t = "函数 f x 则`n返回 x`n结束`n打印 f(1, 2)`n"; r = 'static.arity'; rt = $false },
        @{ n = 'runtime'; t = "函数 甲 则`n乙()`n结束`n函数 乙 则`n打印 1 / 0`n结束`n甲()`n"; r = 'runtime.divide_zero'; rt = $true }
    )
    foreach ($c in $cases) {
        $f = Join-Path $dir ($c.n + '.wen')
        [IO.File]::WriteAllText($f, $c.t, (New-Object Text.UTF8Encoding($false)))
        $pargs = if ($c.rt) { @('--json', '--精简', $f) } else { @('--检查', '--json', '--精简', $f) }
        $r = Invoke-Wen $pargs
        $j = $r.out.Trim() | ConvertFrom-Json
        if ($r.code -ne 1 -or $j.ok -ne $false -or $j.diagnostics[0].rule -ne $c.r) {
            $fail++
            Write-Host ("失败：rule {0}，得到 {1}" -f $c.r, $j.diagnostics[0].rule)
        }
        $props = $j.diagnostics[0].PSObject.Properties.Name
        if ($props -contains 'detail' -or $props -contains 'message') {
            $fail++
            Write-Host ("失败：--精简 仍含 detail/message（{0}）" -f $c.n)
        }
        if ($c.rt -and -not $j.diagnostics[0].frames) {
            $fail++
            Write-Host "失败：运行时 JSON 缺 frames"
        }
    }

    $mf = Join-Path $dir 'machine.wen'
    [IO.File]::WriteAllText($mf, '打印 1 打印 2', (New-Object Text.UTF8Encoding($false)))
    $mr = Invoke-Wen @('--检查', '--机器', $mf)
    $mj = $mr.out.Trim() | ConvertFrom-Json
    $mp = $mj.diagnostics[0].PSObject.Properties.Name
    if (($mp -contains 'kind') -or ($mp -contains 'reason') -or ($mp -contains 'message') -or ($mp -contains 'detail')) {
        $fail++
        Write-Host "失败：--机器 仍输出中文报告字段"
    }
    if ($mj.diagnostics[0].rule -ne 'syntax.need_newline' -or -not $mj.diagnostics[0].sourceLine) {
        $fail++
        Write-Host "失败：--机器 缺 rule/sourceLine"
    }

    $ef = Join-Path $dir 'exprframe.wen'
    [IO.File]::WriteAllText($ef, "函数 甲 则`n返回 乙()`n结束`n函数 乙 则`n返回 丙()`n结束`n函数 丙 则`n返回 1 / 0`n结束`n甲()`n", (New-Object Text.UTF8Encoding($false)))
    $er = Invoke-Wen @('--json', '--精简', $ef)
    $ej = $er.out.Trim() | ConvertFrom-Json
    if (-not $ej.diagnostics[0].frames -or $ej.diagnostics[0].frames.Count -lt 3) {
        $fail++
        Write-Host "失败：表达式内调用缺 frames"
    }

    $rf = Join-Path $dir 'runtime.wen'
    $rb = Invoke-Wen @('--简短', $rf)
    if ($rb.err.Contains('说明：')) {
        $fail++
        Write-Host "失败：--简短 仍打印「说明：」"
    }
    if (-not $rb.err.Contains('调用栈：')) {
        $fail++
        Write-Host "失败：--简短 文本缺调用栈"
    }
} catch {
    $fail++
    Write-Host "失败：错误协议回归异常：$_"
} finally {
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail -gt 0) { exit 1 }
Write-Host "错误协议回归：rule / --精简 / --简短 / frames（语句+表达式）/ --机器 通过"

exit 0
