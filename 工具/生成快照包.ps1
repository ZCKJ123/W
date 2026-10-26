param([string]$Tag = "0.1.0-snapshot")
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outRoot = Join-Path $root '发布\快照'
$dir = Join-Path $outRoot $Tag
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
New-Item -ItemType Directory $dir -Force | Out-Null
Copy-Item (Join-Path $root 'wen.exe') $dir -Force
foreach ($name in @('例子', '库', '模板')) {
    $src = Join-Path $root $name
    if (Test-Path $src) {
        Copy-Item $src $dir -Recurse -Force
    }
}
foreach ($name in @('README.md', '使用教程.md', '使用规范.md', '更新目录.md', '版本验证报告.md', '性能基准.md', 'LICENSE')) {
    $src = Join-Path $root $name
    if (Test-Path $src) { Copy-Item $src $dir -Force }
}
$exe = Join-Path $dir 'wen.exe'
$hash = (Get-FileHash -Algorithm SHA256 $exe).Hash.ToLower()
$files = Get-ChildItem $dir -Recurse -File | ForEach-Object { $_.FullName.Substring($dir.Length + 1) }
$manifest = [ordered]@{
    version = '0.1.0-snapshot'
    tag = $Tag
    created = $stamp
    exe_sha256 = $hash
    files = @($files)
}
$json = $manifest | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $dir '快照.json'), $json, (New-Object Text.UTF8Encoding($true)))
$zip = Join-Path $outRoot ("文语言-" + $Tag + "-windows-x64.zip")
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force
Write-Host "快照包：$zip"
Write-Host "SHA256：$hash"
