$ErrorActionPreference = 'Continue'
# 工具脚本巡检：把 工具\*.wen 都跑一遍，只要求「能跑通」（返回码 0）
# 跳过三类：
#   1) 需要外部输入的（读 C:\Temp\... 里别的工具生成的中间文件）
#   2) 自举求值 那一族：库/自举求值 + 库/自举运行时 是早期原型，
#      依赖的 自举运行时.wen 自己加载就失败（把 打印 当变量名），跟语言本体无关
#   3) 专门用来展示报错的脚本
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$skip = @(
    '闭合检查.wen',
    '词法后缀检查.wen',
    '自举分支求值.wen', '自举基本用例求值.wen', '自举函数求值.wen', '自举函数用例求值.wen',
    '自举列表求值.wen', '自举列表用例求值.wen', '自举内置求值.wen', '自举数学用例求值.wen',
    '自举文本用例求值.wen', '自举异常用例求值.wen', '自举字典用例求值.wen'
)
$fail = 0
$ok = 0
$skipped = 0
$files = Get-ChildItem (Join-Path $root '工具') -Filter *.wen | Sort-Object Name
$sw = [Diagnostics.Stopwatch]::StartNew()
foreach ($f in $files) {
    if ($skip -contains $f.Name) { $skipped++; continue }
    $out = & $exe $f.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) { $ok++ }
    else {
        $first = ($out -split "`r?`n" | Where-Object { $_ -match '原因：' } | Select-Object -First 1)
        Write-Host ("失败：" + $f.Name + "  （" + $LASTEXITCODE + "）  " + ($first -replace '.*原因：', ''))
        $fail++
    }
}
$sw.Stop()
Write-Host ("工具脚本巡检：{0} 个通过，{1} 个失败，{2} 个跳过（共 {3} 个，用时 {4:N0} 秒）" -f `
    $ok, $fail, $skipped, $files.Count, $sw.Elapsed.TotalSeconds)
if ($fail -gt 0) { exit 1 }
exit 0
