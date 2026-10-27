param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$docs = @('使用规范.md', '使用教程.md', '语法总表.md')
$fail = 0
$total = 0
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
