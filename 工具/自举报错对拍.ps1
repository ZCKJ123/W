$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$exe = Join-Path (Get-Location) 'wen.exe'

function Run-Wen([string]$path) {
    $outFile = [IO.Path]::GetTempFileName()
    $errFile = [IO.Path]::GetTempFileName()
    try {
        $p = Start-Process -FilePath $exe -ArgumentList @('"' + $path + '"') `
            -WorkingDirectory (Get-Location) `
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

# 宿主报错的「原因」是一句话，后面可能跟「 建议：…」。求值器只要求含这句原因，
# 不再强求前缀（求值器自己的写法是「用户脚本第 N 行：…」）。
function Reason([string]$text) {
    $m = [regex]::Match($text, '发现【[^】]*】：(.+)')
    if (-not $m.Success) { return '' }
    $r = $m.Groups[1].Value.Trim()
    $k = $r.IndexOf(' 建议：')
    if ($k -ge 0) { $r = $r.Substring(0, $k) }
    return $r.Trim()
}

# 报错归因对拍：同一段源码，宿主报什么原因，自举求值 就必须报同一句。
# 这一族都是「宿主报错、求值器原来静默通过或报成别的话」的真分歧。
$cases = @(
    @{ 名 = '继续在循环外'; 源 = "继续`n" },
    @{ 名 = '中断在循环外'; 源 = "中断`n" },
    @{ 名 = '关键字当变量'; 源 = "将 如果 变成 1`n" },
    @{ 名 = '尝试缺捕获'; 源 = "尝试`n    打印 1`n" },
    @{ 名 = '列表负下标越界'; 源 = "将 列 变成 [1,2]`n打印 列[-3]`n" },
    @{ 名 = '文本负下标越界'; 源 = "打印 `"ab`"[-5]`n" },
    @{ 名 = '抛出非文本'; 源 = "抛出(5)`n" },
    @{ 名 = '负数参数没括号'; 源 = "打印 平方根 -4`n" },
    @{ 名 = '空字典取键'; 源 = "打印 ({}).[`"甲`"]`n" }
)

$tmpTool = Join-Path (Get-Location) ('临时报错对拍_' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.wen')
$bad = @()
try {
    foreach ($case in $cases) {
        # GetTempFileName 会先建一个 .tmp 文件，再拼 .wen 就把它变成垃圾：
        # 每轮 9 例留 9 个空文件。直接由 GUID 生成路径，不预先落盘。
        $srcPath = Join-Path $env:TEMP ('wen_errpair_' + [Guid]::NewGuid().ToString('N').Substring(0, 10) + '.wen')
        [IO.File]::WriteAllText($srcPath, $case.源, [Text.UTF8Encoding]::new($false))
        $hostRun = Run-Wen $srcPath
        if ($hostRun.Code -eq 0) {
            $bad += ("{0}：宿主本应报错，却 rc=0" -f $case.名)
            Remove-Item -LiteralPath $srcPath -ErrorAction SilentlyContinue
            continue
        }
        $want = Reason $hostRun.Error
        if ($want -eq '') {
            $bad += ("{0}：宿主错误里没取到原因" -f $case.名)
            Remove-Item -LiteralPath $srcPath -ErrorAction SilentlyContinue
            continue
        }
        $toolText = "依赖 自举求值`n运行文言(`"$($srcPath.Replace('\', '/'))`")`n"
        [IO.File]::WriteAllText($tmpTool, $toolText, [Text.UTF8Encoding]::new($false))
        $evalRun = Run-Wen $tmpTool
        $got = ($evalRun.Error -replace "`r`n", ' ')
        if ($evalRun.Code -eq 0) {
            $bad += ("{0}：求值器 rc=0 静默通过，宿主说「{1}」" -f $case.名, $want)
        } elseif ($got -notmatch [regex]::Escape($want)) {
            $bad += ("{0}：求值器报「{1}」，宿主说「{2}」" -f $case.名, $got.Trim(), $want)
        }
        Remove-Item -LiteralPath $srcPath -ErrorAction SilentlyContinue
    }
} finally {
    Remove-Item -LiteralPath $tmpTool -ErrorAction SilentlyContinue
}

# 取值对拍：宿主 rc=0 的源码，求值器必须给出逐字相同的输出。
# 这一类是「宿主能跑、求值器跑不了」的语法兼容面（分号连写就是这么发现的）。
$指 = @(
    @{ 名 = '分号连写'; 源 = "将 甲 变成 2; 将 乙 变成 3; 打印 甲 + 乙`n" },
    @{ 名 = '函数体分号'; 源 = "函数 相加 a b 则 返回 a + b; 结束; 打印 相加(2, 3); 打印 相加(4, 5)`n" }
)
foreach ($case in $指) {
    $srcPath = Join-Path $env:TEMP ('wen_valpair_' + [Guid]::NewGuid().ToString('N').Substring(0, 10) + '.wen')
    [IO.File]::WriteAllText($srcPath, $case.源, [Text.UTF8Encoding]::new($false))
    $hostRun = Run-Wen $srcPath
    if ($hostRun.Code -ne 0) {
        $bad += ("{0}：宿主本应跑通，却 rc={1}" -f $case.名, $hostRun.Code)
        Remove-Item -LiteralPath $srcPath -ErrorAction SilentlyContinue
        continue
    }
    $toolText = "依赖 自举求值`n运行文言(`"$($srcPath -replace [regex]::Escape([IO.Path]::DirectorySeparatorChar), '/')`")`n"
    [IO.File]::WriteAllText($tmpTool, $toolText, [Text.UTF8Encoding]::new($false))
    $evalRun = Run-Wen $tmpTool
    $a = ($hostRun.Output -replace "`r`n", "`n").Trim()
    $b = ($evalRun.Output -replace "`r`n", "`n").Trim()
    if ($evalRun.Code -ne 0 -or $a -ne $b) {
        $bad += ("{0}：求值器 rc={1}，输出「{2}」，宿主「{3}」" -f $case.名, $evalRun.Code, $b, $a)
    }
    Remove-Item -LiteralPath $srcPath -ErrorAction SilentlyContinue
}

if ($bad.Count -gt 0) {
    foreach ($b in $bad) { Write-Host ("对拍失败：{0}" -f $b) }
    exit 1
}
Write-Output ("自举报错对拍：{0} 例与宿主同因、{1} 例取值逐字一致" -f $cases.Count, $指.Count)
exit 0
