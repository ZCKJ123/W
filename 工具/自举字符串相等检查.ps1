$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$src = Join-Path $env:TEMP 'wen_selfhost\eq_check.wen'
New-Item -ItemType Directory -Path (Split-Path $src) -Force | Out-Null
@'
依赖 文件库
依赖 自举库
依赖 PE代码库
将 运行 变成 读文件("库/自举运行时.wen")
将 源 变成 运行 + """
返回 字符串相等("ab", "ab") + 字符串相等("ab", "ac")
"""
将 码 变成 编译源字节(源)
打包代码PE("C:/Temp/wen_selfhost/eq.exe", 码)
'@ | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -ne 0) { throw '字符串相等 PE 生成失败' }
$p = Start-Process -FilePath 'C:\Temp\wen_selfhost\eq.exe' -PassThru -Wait
if ($p.ExitCode -ne 1) { throw "字符串相等错误：$($p.ExitCode)" }
Write-Output '自举运行时：字符串相等 通过'
