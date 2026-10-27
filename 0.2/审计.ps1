param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$all = @()
$all += Get-ChildItem (Join-Path $root '例子') -Filter *.wen
$all += Get-ChildItem (Join-Path $root '测试\用例') -Filter *.wen
$zero = @{}
$hits = @()
foreach ($f in $all) {
    $text = [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8)
    foreach ($m in [regex]::Matches($text, '(?m)^函数\s+(\S+)\s+则\s*$')) {
        $zero[$m.Groups[1].Value] = $true
    }
}
foreach ($f in $all) {
    $text = [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8)
    foreach ($name in $zero.Keys) {
        foreach ($m in [regex]::Matches($text, [regex]::Escape($name) + '(?!\s*\()')) {
            $line = ($text.Substring(0, $m.Index) -split "`n").Count
            $lineText = ($text -split "`n")[$line - 1]
            if ($lineText -match '^\s*函数\s') { continue }
            $code = $lineText -replace '"[^"]*"', '' -replace '#.*$', ''
            if ($code -notmatch [regex]::Escape($name)) { continue }
            $hits += [pscustomobject]@{ 文件 = $f.FullName.Substring($root.Length + 1); 规则 = "零参自动调用：$name（第 $line 行）" }
        }
    }
}
$hits | Format-Table -AutoSize
Write-Host ("审计命中：{0} 处" -f $hits.Count)
if ($Apply) {
    $out = Join-Path $root '0.2\自动审计.txt'
    ($hits | ForEach-Object { "$($_.文件)`t$($_.规则)" }) | Set-Content $out -Encoding UTF8
    Write-Host "已写入 $out"
}
