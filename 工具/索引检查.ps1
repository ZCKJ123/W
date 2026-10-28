$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$src = Join-Path $root '工程\三维\程序化.wen'
$text = (& $exe --索引 $src) -join "`n"
$json = (& $exe --索引 --json $src) -join "`n" | ConvertFrom-Json
$fail = 0
if ($text -notmatch '文件 .*程序化\.wen') { $fail++; Write-Host '失败：索引缺文件行' }
if ($text -notmatch '依赖 三维库、视口库、数学库') { $fail++; Write-Host '失败：索引缺依赖' }
if ($text -notmatch '函数 环点\(a b\) 第 2 行') { $fail++; Write-Host '失败：索引缺函数/行号' }
if ($json.functions.Count -lt 1 -or $json.functions[0].name -ne '环点') { $fail++; Write-Host '失败：JSON 索引函数不对' }
if ($json.dependencies.Count -ne 3) { $fail++; Write-Host '失败：JSON 依赖没拆开' }
if ($fail -gt 0) { exit 1 }
Write-Host '索引回归：函数/依赖/行号/JSON 通过'
exit 0
