$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$cases = @(
    @{ N='赋值'; G='将 甲 变成 1'; B='将 甲 变成' },
    @{ N='复合赋值'; G="将 甲 变成 1`n将 甲 += 2"; B='将 甲 +=' },
    @{ N='函数旧形参'; G="函数 f(x) 则`n返回 x`n结束"; B="函数 f(x 则`n返回 x`n结束" },
    @{ N='如果'; G="如果 真 则`n打印 1`n结束"; B="如果 真`n打印 1`n结束" },
    @{ N='当'; G="当 假 则`n打印 1`n结束"; B="当 假`n打印 1`n结束" },
    @{ N='对于'; G="对于 x 在 [1] 中 则`n打印 x`n结束"; B="对于 x 在 [1] 则`n打印 x`n结束" },
    @{ N='返回'; G="函数 f 则`n返回 1`n结束"; B="函数 f 则`n返回 1 2`n结束" },
    @{ N='依赖'; G='依赖 数学库'; B='依赖' },
    @{ N='导出'; G='导出 x'; B='导出' },
    @{ N='打印'; G='打印 1'; B='打印 )' },
    @{ N='逻辑或'; G='打印 真 或 假'; B='打印 或 假' },
    @{ N='比较'; G='打印 1 < 2'; B='打印 1 < < 2' },
    @{ N='加法'; G='打印 1 + 2'; B='打印 1 +' },
    @{ N='乘法'; G='打印 2 * 3'; B='打印 2 *' },
    @{ N='幂'; G='打印 2 ** 3'; B='打印 2 **' },
    @{ N='调用'; G='打印 长度([1])'; B='打印 长度(]' },
    @{ N='索引'; G='打印 [1][0]'; B='打印 [1][' },
    @{ N='切片'; G='打印 [1, 2][0:1]'; B='打印 [1][0:1' },
    @{ N='列表'; G='打印 [1, 2]'; B='打印 [1,' },
    @{ N='字典'; G='打印 {"甲": 1}'; B='打印 {"甲" 1}' },
    @{ N='集合'; G='打印 集合(1, 2)'; B='打印 集合(1,' },
    @{ N='并置'; G="函数 加一 x 则`n返回 x + 1`n结束`n打印 加一 2"; B="函数 加一 x 则`n返回 x + 1`n结束`n打印 加一 2 * 3" },
    @{ N='且'; G='打印 真 且 假'; B='打印 且 假' },
    @{ N='非'; G='打印 非 假'; B='打印 非' },
    @{ N='上标'; G="将 甲 变成 2`n打印 甲²"; B='打印 ²' },
    @{ N='角度'; G='打印 90°'; B='打印 °' },
    @{ N='文本'; G='打印 "甲"'; B='打印 "甲' },
    @{ N='中断'; G="对于 x 在 [1] 中 则`n中断`n结束"; B="对于 x 在 [1] 中 则`n中断 )`n结束" },
    @{ N='继续'; G="对于 x 在 [1] 中 则`n继续`n结束"; B="对于 x 在 [1] 中 则`n继续 )`n结束" },
    @{ N='插值'; G="将 甲 变成 1`n打印 f`"{甲}`""; B='打印 f"{甲"' },
    @{ N='下标赋值'; G="将 表 变成 [1]`n将 表[0] 变成 2"; B='将 表[] 变成 2' },
    @{ N='尝试'; G="尝试`n打印 1`n捕获 e`n打印 2`n结束"; B="尝试`n打印 1" }
)

function Invoke-Check([string]$code) {
    $tmp = [IO.Path]::GetTempFileName() + '.脚本'
    [IO.File]::WriteAllText($tmp, $code, (New-Object Text.UTF8Encoding($false)))
    $out = [IO.Path]::GetTempFileName()
    $err = [IO.Path]::GetTempFileName()
    try {
        $p = Start-Process -FilePath $exe -ArgumentList @('--检查', $tmp) `
            -WorkingDirectory $root -RedirectStandardOutput $out `
            -RedirectStandardError $err -NoNewWindow -Wait -PassThru
        return $p.ExitCode
    } finally {
        Remove-Item $tmp, $out, $err -Force -ErrorAction SilentlyContinue
    }
}

$bad = 0
foreach ($c in $cases) {
    if ((Invoke-Check $c.G) -ne 0) {
        $bad++
        Write-Host ("正例失败：{0}" -f $c.N)
    }
    if ((Invoke-Check $c.B) -eq 0) {
        $bad++
        Write-Host ("反例通过：{0}" -f $c.N)
    }
}
Write-Host ("语法自检：{0} 组，失败 {1} 组" -f $cases.Count, $bad)
if ($bad -gt 0) { exit 1 }
