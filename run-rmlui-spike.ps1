param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root "build\examples\rmlui_spike\$Configuration\vespera_rmlui_spike.exe"

if (-not (Test-Path $Exe)) {
    Write-Host "RmlUi showcase executable is missing; building Vespera first..." -ForegroundColor Yellow
    & (Join-Path $Root "build.ps1") -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path $Exe)) {
    throw "RmlUi showcase executable was not produced: $Exe"
}

Write-Host "Launching Vespera RmlUi showcase..." -ForegroundColor Cyan
Push-Location (Split-Path -Parent $Exe)
try {
    & $Exe
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
