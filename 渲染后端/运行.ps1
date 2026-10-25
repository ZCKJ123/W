param(
    [Parameter(Mandatory = $true)][ValidateSet("OpenGL", "Vulkan")][string]$Backend,
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$OutFile = ""
)
$ErrorActionPreference = "Stop"
if (-not $OutFile) { $OutFile = [IO.Path]::ChangeExtension($Source, ".exe") }
$script = Join-Path $PSScriptRoot ("构建" + $Backend + ".ps1")
& powershell -NoProfile -ExecutionPolicy Bypass -File $script -Source $Source -OutFile $OutFile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$outDir = Split-Path -Parent ([IO.Path]::GetFullPath($OutFile))
Start-Process -FilePath ([IO.Path]::GetFullPath($OutFile)) -WorkingDirectory $outDir
Write-Host "已启动：$OutFile"
