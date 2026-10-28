$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = Join-Path ([IO.Path]::GetTempPath()) ('wen_ts_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null
$fail = 0
try {
    $src = Join-Path $dir 'a.wen'
    $out = Join-Path $dir 'a.ts'
    $runner = Join-Path $dir 'run.wen'
    [IO.File]::WriteAllText($src, "# 外派: typescript`n函数 f x 则`n返回 x * 2`n结束`n打印 `"结果`", f(4)`n", (New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($runner, "依赖 外派库`n外派翻译(`"$src`", `"$out`")`n", (New-Object Text.UTF8Encoding($false)))
    & $exe $runner | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) { $fail++; Write-Host '失败：TypeScript 没生成' }
    else {
        $code = [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8)
        if ($code -notmatch 'function f\(' -or $code -notmatch 'console\.log') {
            $fail++; Write-Host '失败：TypeScript 内容不完整'
        }
        $node = Get-Command node.exe -ErrorAction SilentlyContinue
        if (-not $node) {
            foreach ($cand in @("$env:ProgramFiles\nodejs\node.exe", "${env:ProgramFiles(x86)}\nodejs\node.exe")) {
                if (Test-Path $cand) { $node = Get-Item $cand; break }
            }
        }
        if ($node) {
            $js = Join-Path $dir 'a.js'
            Copy-Item $out $js -Force
            $run = (& $node.Source $js 2>&1) -join "`n"
            if ($LASTEXITCODE -ne 0 -or $run -notmatch '结果 8') {
                $fail++; Write-Host "失败：TypeScript 产物运行不对：$run"
            } else {
                Write-Host "TypeScript 产物运行：$run"
            }
        } else {
            Write-Host 'TypeScript 产物运行跳过：本机没有 node.exe'
        }
    }
} catch {
    $fail++; Write-Host "失败：TypeScript 回归异常：$_"
} finally {
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail -gt 0) { exit 1 }
Write-Host 'TypeScript 回归：生成/函数/输出 通过'
exit 0
