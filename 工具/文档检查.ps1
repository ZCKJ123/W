param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$docs = @('使用规范.md', '使用教程.md', '语法总表.md', '语言规范.md')
$fail = 0
$total = 0
$skipDocs = [ordered]@{
    '破坏性变更.md' = '故意展示旧写法/破坏性示例，不能直接运行'
    '0.2/语法公理.md' = '提案与反例伪代码，含故意不成立的写法'
}
foreach ($sd in $skipDocs.Keys) {
    $sp = Join-Path $root $sd
    if (Test-Path $sp) {
        $n = ([regex]::Matches([IO.File]::ReadAllText($sp, [Text.Encoding]::UTF8), "```wen\r?\n")).Count
        Write-Host ("跳过：{0}（{1} 块）：{2}" -f $sd, $n, $skipDocs[$sd])
    }
}

$ebnfPath = Join-Path $root '语法.ebnf'
$ebnf = [IO.File]::ReadAllText($ebnfPath, [Text.Encoding]::UTF8)
foreach ($name in @('打印', '列表', '字典', '切片', '函数定义', '导出', '赋值目标', '并置后缀')) {
    if ($ebnf -notmatch ("(?m)^" + [regex]::Escape($name) + "\s*=")) {
        $fail++
        Write-Host ("失败：语法.ebnf 缺少产生式「{0}」" -f $name)
    }
}
foreach ($old in @('零参数函数名字一出现', '并置调用只吃', '并置调用只允许吃')) {
    if ($ebnf -match [regex]::Escape($old)) {
        $fail++
        Write-Host ("失败：语法.ebnf 仍含旧规则「{0}」" -f $old)
    }
}
$specDocs = @('语言规范.md', '使用规范.md', '语法总表.md')
$bannedSpec = @(
    '名字出现即调用',
    '不是一等值',
    '只接受一个一元表达式',
    '`加一 2 * 3` 是 `(加一 2)',
    '源码必须是 UTF-8',
    '`.wen` 只作为兼容后缀'
)
foreach ($doc in $specDocs) {
    $p = Join-Path $root $doc
    if (-not (Test-Path $p)) { continue }
    $body = [IO.File]::ReadAllText($p, [Text.Encoding]::UTF8)
    foreach ($bad in $bannedSpec) {
        if ($body.Contains($bad)) {
            $fail++
            Write-Host ("失败：{0} 仍含 0.1 旧规则「{1}」" -f $doc, $bad)
        }
    }
}

foreach ($doc in $docs) {
    $path = Join-Path $root $doc
    if (-not (Test-Path $path)) { continue }
    $text = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
    $matches = [regex]::Matches($text, '```wen\r?\n(.*?)```', 'Singleline')
    $i = 0
    foreach ($m in $matches) {
        $i++
        $code = $m.Groups[1].Value
        if ($code -match '…|\.\.\.|【|待填写|省略|语法错误|禁止|报错') { continue }
        if ($code -match '(新建集合|取输入|变量名单|开启内存|关闭内存|开启调用统计)(?!\s*\()') {
            $fail++
            Write-Host ("失败：{0} 第 {1} 个代码块含零参裸调用" -f $doc, $i)
            continue
        }
        $mix = [regex]::Match($code, '(?m)^\s*([\u4e00-\u9fa5A-Za-z_][\u4e00-\u9fa5A-Za-z0-9_]*)\s+([\u4e00-\u9fa5A-Za-z0-9_]+)\s*(\*|/|//|%)')
        if ($mix.Success -and $mix.Groups[1].Value -notin @('打印','将','如果','当','对于','返回','依赖','函数','结束','否则','尝试','捕获','存入','运行','读取','写文件')) {
            $fail++
            Write-Host ("失败：{0} 第 {1} 个代码块疑似并置混写：{2}" -f $doc, $i, $mix.Value.Trim())
            continue
        }
        $total++
        $tmp = [IO.Path]::GetTempFileName() + '.脚本'
        [IO.File]::WriteAllText($tmp, $code, (New-Object Text.UTF8Encoding($false)))
        $outFile = [IO.Path]::GetTempFileName()
        $errFile = [IO.Path]::GetTempFileName()
        try {
            $p = Start-Process -FilePath $exe -ArgumentList @('--检查', '--不查名字', $tmp) `
                -WorkingDirectory $root -RedirectStandardOutput $outFile `
                -RedirectStandardError $errFile -NoNewWindow -Wait -PassThru
            $out = [IO.File]::ReadAllText($errFile, [Text.Encoding]::UTF8)
            if ($p.ExitCode -ne 0) {
                $fail++
                Write-Host ("失败：{0} 第 {1} 个代码块" -f $doc, $i)
                $out -split "`n" | Select-Object -First 4 | ForEach-Object { Write-Host ("  " + $_) }
            }
        } finally {
            Remove-Item $tmp, $outFile, $errFile -Force -ErrorAction SilentlyContinue
        }
    }
}
$tmpErr = [IO.Path]::GetTempFileName() + '.wen'
[IO.File]::WriteAllText($tmpErr, '打印 +' + "`n", (New-Object Text.UTF8Encoding($false)))
$jout = [IO.Path]::GetTempFileName()
$jerr = [IO.Path]::GetTempFileName()
try {
    $jp = Start-Process -FilePath $exe -ArgumentList @('--检查', '--json', $tmpErr) `
        -WorkingDirectory $root -RedirectStandardOutput $jout -RedirectStandardError $jerr `
        -NoNewWindow -Wait -PassThru
    $body = [IO.File]::ReadAllText($jout, [Text.Encoding]::UTF8).Trim()
    $obj = $body | ConvertFrom-Json
    if ($jp.ExitCode -ne 1 -or $obj.ok -ne $false -or
        $obj.diagnostics[0].code -ne 'WEN-SYN-001' -or
        $obj.diagnostics[0].severity -ne 'error') {
        $fail++
        Write-Host "失败：--检查 --json 错误协议不符合（code/severity/exit）"
    } else {
        Write-Host "错误协议：通过（WEN-SYN-001 / error / exit 1）"
    }
} catch {
    $fail++
    Write-Host "失败：--检查 --json 协议检查异常：$_"
} finally {
    Remove-Item $tmpErr, $jout, $jerr -Force -ErrorAction SilentlyContinue
}
# 错误输出回归：rule / --精简 / --简短 / frames
$regDir = Join-Path ([IO.Path]::GetTempPath()) ('wen_err_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $regDir | Out-Null
$regCases = @(
    @{ name = 'newline'; text = '打印 1 打印 2'; rule = 'syntax.need_newline'; runtime = $false },
    @{ name = 'undefined'; text = '打印 不存在'; rule = 'static.undefined_name'; runtime = $false },
    @{ name = 'arity'; text = "函数 f x 则`n返回 x`n结束`n打印 f(1, 2)`n"; rule = 'static.arity'; runtime = $false },
    @{ name = 'runtime'; text = "函数 甲 则`n乙()`n结束`n函数 乙 则`n打印 1 / 0`n结束`n甲()`n"; rule = 'runtime.divide_zero'; runtime = $true }
)
try {
    foreach ($c in $regCases) {
        $fp = Join-Path $regDir ($c.name + '.wen')
        [IO.File]::WriteAllText($fp, $c.text, (New-Object Text.UTF8Encoding($false)))
        $out = Join-Path $regDir ($c.name + '.json')
        $err = Join-Path $regDir ($c.name + '.err')
        $pargs = if ($c.runtime) { @('--json', '--精简', $fp) } else { @('--检查', '--json', '--精简', $fp) }
        $rp = Start-Process -FilePath $exe -ArgumentList $pargs -WorkingDirectory $root `
            -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow -Wait -PassThru
        $j = [IO.File]::ReadAllText($out, [Text.Encoding]::UTF8).Trim() | ConvertFrom-Json
        if ($rp.ExitCode -ne 1 -or $j.ok -ne $false -or $j.diagnostics[0].rule -ne $c.rule) {
            $fail++
            Write-Host ("失败：错误回归 rule {0}（得到 {1}）" -f $c.rule, $j.diagnostics[0].rule)
        }
        if ($j.diagnostics[0].PSObject.Properties.Name -contains 'detail' -or
            $j.diagnostics[0].PSObject.Properties.Name -contains 'message') {
            $fail++
            Write-Host ("失败：--精简 仍含 detail/message（{0}）" -f $c.name)
        }
        if ($c.runtime -and -not $j.diagnostics[0].frames) {
            $fail++
            Write-Host "失败：运行时 JSON 缺少 frames 调用栈"
        }
    }
    $briefOut = Join-Path $regDir 'brief.out'
    $briefErr = Join-Path $regDir 'brief.err'
    Start-Process -FilePath $exe -ArgumentList @('--简短', (Join-Path $regDir 'runtime.wen')) `
        -WorkingDirectory $root -RedirectStandardOutput $briefOut -RedirectStandardError $briefErr `
        -NoNewWindow -Wait | Out-Null
    $briefText = [IO.File]::ReadAllText($briefErr, [Text.Encoding]::UTF8)
    if ($briefText.Contains('说明：')) {
        $fail++
        Write-Host "失败：--简短 仍在打印「说明：」"
    }
    if ($fail -eq 0) { Write-Host "错误回归：rule / --精简 / --简短 / frames 通过" }
} catch {
    $fail++
    Write-Host "失败：错误回归异常：$_"
} finally {
    Remove-Item $regDir -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host ("文档代码块：{0} 个，失败 {1} 个" -f $total, $fail)
if ($fail -gt 0) { exit 1 }
