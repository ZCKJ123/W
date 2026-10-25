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
$glsl = $null
$gcmd = Get-Command glslangValidator -ErrorAction SilentlyContinue
if ($gcmd) { $glsl = $gcmd.Source }
if (-not $glsl) {
    $candidate = "C:\msys64\ucrt64\bin\glslangValidator.exe"
    if (Test-Path $candidate) {
        $glsl = $candidate
        $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
    }
}
if (-not $gpp -or -not $glsl) {
    Write-Host "找不到 g++ 或 glslangValidator。请安装 MSYS2 + Vulkan 工具。"
    exit 1
}

$srcDir = Split-Path -Parent (Resolve-Path $Source)
$workDir = Join-Path $env:TEMP ("wen_vk_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $workDir | Out-Null
Copy-Item $Source (Join-Path $workDir "main.cpp") -Force
foreach ($f in @("scene.vert", "scene.frag")) {
    $p = Join-Path $srcDir $f
    if (-not (Test-Path $p)) { $p = Join-Path "渲染后端" $f }
    Copy-Item $p (Join-Path $workDir $f) -Force
}
Push-Location $workDir
& $glsl -V --target-env vulkan1.3 "scene.vert" -o "scene.vert.spv" | Out-Null
$c1 = $LASTEXITCODE
& $glsl -V --target-env vulkan1.3 "scene.frag" -o "scene.frag.spv" | Out-Null
$c2 = $LASTEXITCODE
if ($c1 -ne 0 -or $c2 -ne 0) {
    Write-Host "着色器编译失败。"
    Pop-Location
    exit 1
}
& $gpp -std=c++17 -O2 -static-libgcc -static-libstdc++ "main.cpp" -o "scene.exe" -lvulkan-1
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) {
    Write-Host "编译失败。"
    Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue
    exit $code
}
Copy-Item (Join-Path $workDir "scene.exe") $OutFile -Force
$outDir = Split-Path -Parent ([IO.Path]::GetFullPath($OutFile))
Copy-Item (Join-Path $workDir "scene.vert.spv") (Join-Path $outDir "scene.vert.spv") -Force
Copy-Item (Join-Path $workDir "scene.frag.spv") (Join-Path $outDir "scene.frag.spv") -Force
Copy-Item "C:\msys64\ucrt64\bin\libwinpthread-1.dll" (Join-Path $outDir "libwinpthread-1.dll") -Force -ErrorAction SilentlyContinue
Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "编译成功：$OutFile"
