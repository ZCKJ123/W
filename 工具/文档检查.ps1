param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$docs = @('使用规范.md', '使用教程.md', '语法总表.md')
$fail = 0
$total = 0

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
            $p = Start-Process -FilePath $exe -ArgumentList @('--检查', $tmp) `
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
Write-Host ("文档代码块：{0} 个，失败 {1} 个" -f $total, $fail)
if ($fail -gt 0) { exit 1 }
