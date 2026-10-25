param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$OutFile = ""
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path $Source)) {
    Write-Host "找不到源文件：$Source"
    exit 1
}
if (-not $OutFile) { $OutFile = [IO.Path]::ChangeExtension($Source, ".exe") }

$gpp = $null
$cmd = Get-Command g++ -ErrorAction SilentlyContinue
if ($cmd) { $gpp = $cmd.Source }
if (-not $gpp) {
    $candidate = "C:\msys64\ucrt64\bin\g++.exe"
    if (Test-Path $candidate) {
        $gpp = $candidate
        $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
    }
}
if (-not $gpp) {
    Write-Host "找不到 g++。请安装 MSYS2，或把 g++ 放进 PATH。"
    exit 1
}

# 在 ASCII 临时目录编译，避免中文路径让 ld 打不开输出文件。
$workDir = Join-Path $env:TEMP ("wen_gl_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $workDir | Out-Null
Copy-Item $Source (Join-Path $workDir "scene.cpp") -Force
Push-Location $workDir
& $gpp -std=c++17 -O2 -static-libgcc -static-libstdc++ "scene.cpp" -o "scene.exe" -lopengl32 -lgdi32
$exitCode = $LASTEXITCODE
Pop-Location
if ($exitCode -ne 0) {
    Write-Host "编译失败。"
    Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue
    exit $exitCode
}
Copy-Item (Join-Path $workDir "scene.exe") $OutFile -Force
$outDir = Split-Path -Parent ([IO.Path]::GetFullPath($OutFile))
Copy-Item "C:\msys64\ucrt64\bin\libwinpthread-1.dll" (Join-Path $outDir "libwinpthread-1.dll") -Force -ErrorAction SilentlyContinue
Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "编译成功：$OutFile"
