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

function Normalize([string]$t) {
    return (($t -replace "`r`n", "`n").TrimEnd())
}

# 求值器历史上出过一个大坑：所有 打印 都写进临时文件，11 个工具 rc=0 而 stdout 一行没有，
# 只看退出码等于没守。所以这里除了退出码，还要求「输出非空」。
$tools = Get-ChildItem '工具' -Filter '*求值.wen' | Sort-Object Name
$empty = @()
foreach ($t in $tools) {
    $r = Run-Wen $t.FullName
    if ($r.Code -ne 0) {
        throw ("求值工具失败：{0} 退出码 {1}`n{2}" -f $t.Name, $r.Code, $r.Error.Trim())
    }
    if ((Normalize $r.Output).Length -eq 0) {
        $empty += $t.Name
    }
}
if ($empty.Count -gt 0) {
    throw ('求值工具没有输出（输出通道断了）：' + ($empty -join '、'))
}

# 和宿主直接跑同一份用例的输出逐行对拍，防止「有输出但算错」。
$pairs = @(
    @{ 工具 = '工具/自举基本用例求值.wen'; 用例 = '测试/用例/01基本.wen' },
    @{ 工具 = '工具/自举列表用例求值.wen'; 用例 = '测试/用例/04列表.wen' },
    @{ 工具 = '工具/自举数学用例求值.wen'; 用例 = '测试/用例/08数学.wen' },
    @{ 工具 = '工具/自举数据格式求值.wen'; 用例 = '测试/用例/10数据格式.wen' },
    @{ 工具 = '工具/自举反编译求值.wen'; 用例 = '测试/用例/46反编译.wen' },
    @{ 工具 = '工具/自举后缀求值.wen'; 用例 = '测试/用例/43后缀.wen' }
)
foreach ($pair in $pairs) {
    $a = Normalize (Run-Wen (Join-Path (Get-Location) $pair.工具)).Output
    $b = Normalize (Run-Wen (Join-Path (Get-Location) $pair.用例)).Output
    if ($a -ne $b) {
        throw ("求值结果与宿主不一致：{0} vs {1}" -f $pair.工具, $pair.用例)
    }
}

Write-Output ("自举求值：{0} 个工具全部有输出，{1} 个用例与宿主逐行一致" -f $tools.Count, $pairs.Count)
