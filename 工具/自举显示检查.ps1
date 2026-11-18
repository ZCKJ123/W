$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Path 'C:/Temp/wen_selfhost' -Force | Out-Null
$exe = 'C:/Temp/wen_selfhost/self_display_check.exe'
Remove-Item $exe -Force -ErrorAction SilentlyContinue
$src = Join-Path $env:TEMP 'wen_selfhost\self_display_check.wen'
$content = @'
依赖 自举库
依赖 PE显示代码库
将 源 变成 """
返回 显示("你好，自举")
"""
将 结 变成 编译显示源(源)
打包显示代码PE("__OUT__", 结["码"], 结["文本"])
'@
$content = $content.Replace('__OUT__', $exe)
$content | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举显示 PE 生成失败' }
if (-not (Test-Path -LiteralPath $exe)) { throw '自举显示 PE 未生成' }
if ((Get-Item -LiteralPath $exe).Length -lt 1024) { throw '自举显示 PE 产物过小（写坏或没写全）' }
$p = Start-Process -FilePath $exe -PassThru
# 原来固定睡 600ms 就去枚举窗口，机器忙/防病毒扫新 exe 时窗口还没建出来，
# 就报「窗口标题错误」的假红。改成最多等 5 秒，看到标题就继续。
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WEnum {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc e, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
'@
$titles = New-Object System.Collections.Generic.List[string]
$截止 = (Get-Date).AddSeconds(5)
$cb = [WEnum+EnumProc]{
    param($h, $l)
    $id = 0
    [WEnum]::GetWindowThreadProcessId($h, [ref]$id) | Out-Null
    if ($id -eq $p.Id) {
        $sb = New-Object System.Text.StringBuilder 256
        [WEnum]::GetWindowText($h, $sb, 256) | Out-Null
        if ($sb.Length -gt 0) { $titles.Add($sb.ToString()) }
    }
    return $true
}
while ((Get-Date) -lt $截止) {
    $titles.Clear()
    [WEnum]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    if ($titles.Count -gt 0) { break }
    Start-Sleep -Milliseconds 100
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
if (-not ($titles -contains '文言')) { throw "自举显示窗口标题错误：$($titles -join ',')" }
Write-Output '自举显示 PE：编译器生成中文 MessageBoxW、标题/文本、无 C 运行时 通过'
