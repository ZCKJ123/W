param([string]$Tests = '未运行')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot\..
$exe = Join-Path (Get-Location) 'wen.exe'
$verify = & $exe --验证
$plugin = & $exe --验证插件 '插件\wen-check'
$version = & $exe --版本
$verText = ($version -join ' ')
$ver = if ($verText -match '(\d+\.\d+\.\d+[^\s]*)') { $matches[1] } else { 'unknown' }
$hash = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower()
$size = (Get-Item $exe).Length
$test = $Tests
$lines = @(
    '# 版本验证报告',
    '',
    "- 生成时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "- 版本：``$ver``",
    "- 测试：$test",
    '',
    '## wen.exe',
    '',
    '```text',
    ($verify -join "`n"),
    '```',
    '',
    "- SHA256：``$hash``",
    "- 大小：$size 字节",
    '',
    '## 插件',
    '',
    '```text',
    ($plugin -join "`n"),
    '```',
    '',
    '## 版本输出',
    '',
    '```text',
    ($version -join "`n"),
    '```',
    '',
    '## 判定',
    '',
    '官方签名、内嵌标记、版本号三者都通过才视为可运行。',
    '版本号不完全一致但主版本 `0.1` 一致时，报告为“版本不匹配”，不当作官方构建。',
    '非官方且版本也不匹配的二进制拒绝运行。'
)
[IO.File]::WriteAllLines((Join-Path (Get-Location) '版本验证报告.md'), $lines, (New-Object Text.UTF8Encoding($true)))
Write-Host '已生成：版本验证报告.md'
