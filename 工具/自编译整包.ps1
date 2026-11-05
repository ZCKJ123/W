# 工具/自编译整包.ps1
# 把 自编译前置 + 自举运行时 + 解析库 + 自举库 拼成一个源文件，
# 用 wen.exe 的自举库编译它，输出机器码长度。
# 这就是「编译器编译自己」的第一步：整包能编出机器码，才谈得上打包成 exe。

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$utf8 = New-Object System.Text.UTF8Encoding($false)
$files = @('库\自举运行时.wen', '库\解析库.wen', '库\自举库.wen')

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine([System.IO.File]::ReadAllText((Join-Path $root '工程\自编译前置.wen'), $utf8))
foreach ($f in $files) {
    foreach ($line in [System.IO.File]::ReadAllLines((Join-Path $root $f), $utf8)) {
        # 依赖/导出 只对解释器的库加载器有意义，编译器不认
        if ($line -notmatch '^\s*(依赖|导出)\s') { [void]$sb.AppendLine($line) }
    }
}

$bundle = Join-Path $root '工程\_自编译整包.源'
[System.IO.File]::WriteAllText($bundle, $sb.ToString(), $utf8)
Write-Host ("整包源：" + [math]::Round((Get-Item $bundle).Length / 1KB, 1) + " KB")

$probe = Join-Path $root '工程\_自编译探针.脚本'
$script = @'
依赖 自举库
依赖 文件库
将 源 变成 读文件("工程/_自编译整包.源")
尝试
    将 码 变成 编译源字节(源)
    打印 长度(码) / 2
捕获 错
    打印 -1
    打印 错["原因"]
结束
'@
[System.IO.File]::WriteAllText($probe, $script, $utf8)

$sw = [Diagnostics.Stopwatch]::StartNew()
$out = & (Join-Path $root 'wen.exe') '工程\_自编译探针.脚本' 2>&1
$sw.Stop()
$code = ($out | Select-Object -First 1)
$bytes = [int]$code
if ($bytes -lt 0) {
    Write-Host "整包编译失败：" ($out | Select-Object -Skip 1) -ForegroundColor Red
    exit 1
}
Write-Host ("整包机器码：" + $bytes + " 字节，用时 " + [math]::Round($sw.Elapsed.TotalSeconds, 2) + " 秒") -ForegroundColor Green
