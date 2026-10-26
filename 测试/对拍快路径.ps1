param([int]$Cases = 1000)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$tmp = Join-Path $env:TEMP ('wen_diff_' + [guid]::NewGuid().ToString('N') + '.wen')
$fail = 0
function Compare-Case([string]$src, [string]$label) {
    [IO.File]::WriteAllText($tmp, $src, (New-Object Text.UTF8Encoding($false)))
    $fast = (& $exe $tmp 2>&1 | Out-String).Trim()
    $slow = (& $exe --不快 $tmp 2>&1 | Out-String).Trim()
    if ($fast -ne $slow) {
        $script:fail++
        Write-Host "FAIL $label"
        Write-Host "  fast=$fast"
        Write-Host "  slow=$slow"
    }
}
for ($i = 0; $i -lt $Cases; $i++) {
    $a = Get-Random -Minimum -1000 -Maximum 1001
    $s = Get-Random -Minimum -1000 -Maximum 1001
    $span = Get-Random -Minimum 0 -Maximum 200
    $n = $s + $span
    $op = if ((Get-Random -Minimum 0 -Maximum 2) -eq 0) { '小于' } else { '小于等于' }
    $src = @"
将 和 变成 $a
将 甲 变成 $s
当 甲 $op $n 则
    将 和 变成 和 + 甲
    将 甲 变成 甲 + 1
结束
打印 数值文本(和), 甲
"@
    Compare-Case $src "a=$a s=$s op=$op n=$n"
    if ($fail -ge 5) { break }
}
# 大次数 + 浮点初值，专门覆盖闭式路径
$values = @(0, 0.1, -0.1, 1000000000000000.1)
foreach ($a in $values) {
    $src = @"
将 和 变成 $a
将 甲 变成 0
当 甲 小于 1000000 则
    将 和 变成 和 + 甲
    将 甲 变成 甲 + 1
结束
打印 数值文本(和), 甲
"@
    Compare-Case $src "large a=$a"
}
Remove-Item $tmp -ErrorAction SilentlyContinue
if ($fail -gt 0) {
    Write-Host "对拍失败：$fail"
    exit 1
}
Write-Host "对拍通过：$Cases 组 + 大次数边界"
