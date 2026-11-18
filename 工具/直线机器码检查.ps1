$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$exe = Join-Path $env:TEMP '直线检查.exe'
$src = Join-Path $env:TEMP '直线检查.wen'
$cases = @(
    @{ 源 = '返回 7'; 期望 = 7 },
    @{ 源 = '返回 1 + 2 * 3'; 期望 = 7 },
    @{ 源 = '返回 10 - 2 * 3'; 期望 = 4 },
    @{ 源 = '返回 7 % 3'; 期望 = 1 },
    @{ 源 = '返回 7 // 3'; 期望 = 2 },
    @{ 源 = '返回 7 / 2'; 期望 = 3 }
)
foreach ($case in $cases) {
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
    $code = $case.源
    $content = @"
依赖 直线
将 码 变成 直线编译("$code")
直线编译机器码(码, "$($exe.Replace('\','/'))")
"@
    $content | Set-Content -Path $src -Encoding UTF8
    & .\wen.exe $src | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "直线编译机器码失败：$($case.源)" }
    if (-not (Test-Path $exe)) { throw "未生成 PE：$($case.源)" }
    $p = Start-Process -FilePath $exe -PassThru -Wait
    if ($p.ExitCode -ne $case.期望) {
        throw "退出码错误：$($case.源) → $($p.ExitCode)，期望 $($case.期望)"
    }
}
# 幂运算没有单条 x86-64 指令，必须明确报错而不是静默算成取余
Remove-Item $exe -Force -ErrorAction SilentlyContinue
$content = @"
依赖 直线
直线编译机器码(直线编译("返回 2 ** 3"), "$($exe.Replace('\','/'))")
"@
$content | Set-Content -Path $src -Encoding UTF8
& .\wen.exe $src | Out-Null
if ($LASTEXITCODE -eq 0) { throw '幂运算 ** 应报错，实际编译成功（会被静默算成取余）' }
Remove-Item $exe, $src -Force -ErrorAction SilentlyContinue
Write-Output '直线编译机器码：6 例源码 → x86-64 字节 → PE 退出码正确；幂运算正确拒绝'
