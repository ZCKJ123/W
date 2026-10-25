param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path $Root).Path
$cpp = [IO.File]::ReadAllText((Join-Path $Root "wen.cpp"), [Text.Encoding]::UTF8)
$libs = @(
    "数学库","概率库","物理库","图形库","渲染库","加速库","外派库","跃迁库",
    "内存库","翻译器","函数库","文件库","输入库","集合库","点阵库","频道库",
    "注入库","可视化库","网络库","网页库","数据格式库","CPU库"
)
$map = @{}
foreach ($m in [regex]::Matches($cpp, '\{"([^"]+)",\s*"([^"]+)"\}')) {
    $name = $m.Groups[1].Value
    $lib = $m.Groups[2].Value
    if ($libs -contains $lib) { $map[$name] = $lib }
}
$constants = @("圆周率","π","自然常数","重力加速度","光速","万有引力常数","声速","普朗克常数","电子电荷","阿伏伽德罗常数","理想气体常数","静电力常量")
foreach ($c in $constants) { if ($map.ContainsKey($c)) { $map.Remove($c) } }
$usageFiles = @()
foreach ($dir in @("例子", "测试\用例", "测试\错误", "测试\库")) {
    $p = Join-Path $Root $dir
    if (Test-Path $p) {
        $usageFiles += Get-ChildItem $p -Filter *.wen -File
    }
}
$texts = @()
foreach ($f in $usageFiles) {
    $texts += [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8)
}
$rows = @()
foreach ($kv in $map.GetEnumerator() | Sort-Object Value, Key) {
    $name = $kv.Key
    $lib = $kv.Value
    $count = 0
    foreach ($t in $texts) {
        if ($t.Contains($name)) { $count++ }
    }
    $rows += [pscustomobject]@{ 库 = $lib; 函数 = $name; 用例数 = $count }
}
$total = $rows.Count
$zero = @($rows | Where-Object { $_.用例数 -eq 0 })
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine("# 函数使用审计")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("扫描范围：例子、测试/用例、测试/错误、测试/库 的 .wen 文件。")
[void]$sb.AppendLine("总函数：$total；零引用：$($zero.Count)。")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("| 库 | 函数数 | 零引用 |")
[void]$sb.AppendLine("|---|---:|---:|")
foreach ($lib in $libs) {
    $all = @($rows | Where-Object { $_.库 -eq $lib })
    if ($all.Count -eq 0) { continue }
    $z = @($all | Where-Object { $_.用例数 -eq 0 })
    [void]$sb.AppendLine("| $lib | $($all.Count) | $($z.Count) |")
}
[void]$sb.AppendLine("")
[void]$sb.AppendLine("## 零引用清单")
[void]$sb.AppendLine("")
foreach ($lib in $libs) {
    $z = @($rows | Where-Object { $_.库 -eq $lib -and $_.用例数 -eq 0 } | Sort-Object 函数)
    if ($z.Count -eq 0) { continue }
    [void]$sb.AppendLine("**$lib**（$($z.Count)）")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine(($z | ForEach-Object { $_.函数 }) -join "、")
    [void]$sb.AppendLine("")
}
$out = Join-Path $Root "审计报告.md"
[IO.File]::WriteAllText($out, $sb.ToString(), (New-Object Text.UTF8Encoding($true)))
Write-Host "已生成：$out"
Write-Host "总函数 $total，零引用 $($zero.Count)"
