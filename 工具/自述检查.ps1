$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$text = (& (Join-Path $root 'wen.exe') --自述) -join "`n"
$fail = 0
if ($text -notmatch '语义根：语法\.ebnf') { $fail++; Write-Host '失败：自述缺语法根' }
if ($text -notmatch '宿主契约：平台接口\.md') { $fail++; Write-Host '失败：自述缺平台契约' }
if ($text -notmatch '错误协议：错误协议\.md') { $fail++; Write-Host '失败：自述缺错误协议' }
if (-not (Test-Path (Join-Path $root '平台接口.md'))) { $fail++; Write-Host '失败：平台接口.md 不存在' }
if ($fail -gt 0) { exit 1 }
Write-Host '自述回归：语义根 / 协议 / 平台契约 通过'
exit 0
