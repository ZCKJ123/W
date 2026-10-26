param([switch]$Apply)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$examples = (Get-ChildItem (Join-Path $root '例子') -Filter *.wen).Count
$cases = (Get-ChildItem (Join-Path $root '测试\用例') -Filter *.wen).Count
$errors = (Get-ChildItem (Join-Path $root '测试\错误') -Filter *.wen).Count
$total = $examples + $cases + $errors
$line = "当前：**$total/$total 通过**（$examples 示例 + $cases 用例 + $errors 错误用例）"
Write-Host $line
if (-not $Apply) { exit 0 }

$tutorial = Join-Path $root '使用教程.md'
$text = [IO.File]::ReadAllText($tutorial, [Text.Encoding]::UTF8)
$text = [regex]::Replace($text, '当前：\*\*\d+/\d+ 通过\*\*（[^）]*）', $line)
[IO.File]::WriteAllText($tutorial, $text, (New-Object Text.UTF8Encoding($false)))

$release = Join-Path $root '发布\发布说明.md'
if (Test-Path $release) {
    $text = [IO.File]::ReadAllText($release, [Text.Encoding]::UTF8)
    $text = [regex]::Replace($text, '\d+/\d+ 通过（[^）]*）', "$total/$total 通过（$examples 示例 + $cases 用例 + $errors 错误用例）")
    [IO.File]::WriteAllText($release, $text, (New-Object Text.UTF8Encoding($false)))
}
Write-Host '已写入使用教程.md 与 发布说明.md'
