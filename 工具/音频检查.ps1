$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'wen.exe'
$dir = Join-Path ([IO.Path]::GetTempPath()) ('wen_audio_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null
$fail = 0
try {
    $wav = Join-Path $dir 'tone.wav'
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter($ms)
    $rate = 8000; $n = 2000
    $bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $n*2))
    $bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $bw.Write([int]16)
    $bw.Write([int16]1); $bw.Write([int16]1); $bw.Write([int]$rate); $bw.Write([int]($rate*2))
    $bw.Write([int16]2); $bw.Write([int16]16)
    $bw.Write([Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]($n*2))
    for ($i=0; $i -lt $n; $i++) {
        $bw.Write([int16](12000 * [Math]::Sin(2*[Math]::PI*440*$i/$rate)))
    }
    $bw.Flush(); [IO.File]::WriteAllBytes($wav, $ms.ToArray()); $bw.Dispose(); $ms.Dispose()

    $runner = Join-Path $dir 'run.wen'
    [IO.File]::WriteAllText($runner, "依赖 媒体库`n打印 音频解析(`"$wav`")`n打印 音频判定(`"$wav`")`n打印 音频频谱(`"$wav`", 1)`n打印 音频轨道(`"$wav`")`n", (New-Object Text.UTF8Encoding($false)))
    $lines = @(& $exe $runner 2>&1)
    $text = $lines -join "`n"
    if ($text -notmatch '"采样率": 8000' -or $text -notmatch '"声道": 1' -or $text -notmatch '"时长": 0\.25') { $fail++; Write-Host '失败：WAV 元数据不对' }
    if ($text -notmatch '"有声音": 真' -or $text -notmatch '"静音": 假') { $fail++; Write-Host '失败：音频判定不对' }
    if ($text -notmatch '"主频": 44\d') { $fail++; Write-Host '失败：主频不对' }
    if ($text -notmatch '"频率": 44\d') { $fail++; Write-Host '失败：频谱不对' }
} catch {
    $fail++; Write-Host "失败：音频回归异常：$_"
} finally {
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if ($fail -gt 0) { exit 1 }
Write-Host '音频回归：WAV 元数据/判定/主频/频谱/轨道 通过'
exit 0
