$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$exe = 'C:/Temp/pe_msg_check.exe'
Remove-Item $exe -Force -ErrorAction SilentlyContinue
$src = Join-Path $env:TEMP 'pe_msg_check.wen'
$content = @'
依赖 PE消息库
打包消息PE("__OUT__", "你好，文言", "文言文本")
'@
$content = $content.Replace('__OUT__', $exe)
$content | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -ne 0) { throw '消息 PE 生成失败' }
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Milliseconds 600
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WinEnum {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc e, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
'@
$titles = New-Object System.Collections.Generic.List[string]
$cb = [WinEnum+EnumProc]{
    param($h, $l)
    $pid2 = 0
    [WinEnum]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($pid2 -eq $p.Id) {
        $sb = New-Object System.Text.StringBuilder 256
        [WinEnum]::GetWindowText($h, $sb, 256) | Out-Null
        if ($sb.Length -gt 0) { $titles.Add($sb.ToString()) }
    }
    return $true
}
[WinEnum]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Stop-Process -Id $p.Id -Force
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
if (-not ($titles -contains '文言文本')) { throw "消息框标题错误：$($titles -join ',')" }
Write-Output '消息 PE：中文标题/内容、UTF-16、无 C 运行时 通过'
