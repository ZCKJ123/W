$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
New-Item -ItemType Directory -Path 'C:/Temp/wen_selfhost' -Force | Out-Null
$exe = 'C:/Temp/wen_selfhost/self_window_check.exe'
Remove-Item $exe -Force -ErrorAction SilentlyContinue
$src = Join-Path $env:TEMP 'wen_selfhost\self_window_check.wen'
$content = @'
依赖 自举库
依赖 PE窗口调用库
将 源 变成 """
将 主 变成 窗口()
返回 主
"""
将 码 变成 编译源字节(源)
打包窗口代码PE("__OUT__", 码)
'@
$content = $content.Replace('__OUT__', $exe)
$content | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -ne 0) { throw '自举窗口 PE 生成失败' }
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Milliseconds 500
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WFind {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc e, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$titles = New-Object System.Collections.Generic.List[string]
$hwnd = [IntPtr]::Zero
$cb = [WFind+EnumProc]{
    param($h, $l)
    $id = 0
    [WFind]::GetWindowThreadProcessId($h, [ref]$id) | Out-Null
    if ($id -eq $p.Id -and [WFind]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 256
        [WFind]::GetWindowText($h, $sb, 256) | Out-Null
        if ($sb.Length -gt 0) { $titles.Add($sb.ToString()) }
        if ($sb.ToString() -eq '文言') { $script:hwnd = $h }
    }
    return $true
}
[WFind]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($hwnd -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; throw '未找到标题为“文言”的窗口' }
[WFind]::PostMessage($hwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 600
$p.Refresh()
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; throw 'WM_CLOSE 后进程未退出' }
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
if (-not ($titles -contains '文言')) { throw "自举窗口标题错误：$($titles -join ',')" }
Write-Output '自举窗口 PE：CreateWindowExW、可见、标题文言、WM_CLOSE 退出、无 C 运行时 通过'
