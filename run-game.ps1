$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root "build\examples\reference_game\Debug\vespera_reference_game.exe"

if (-not (Test-Path $Exe)) {
    Write-Host "Reference game is not built yet. Running build.ps1 first..." -ForegroundColor Yellow
    & (Join-Path $Root "build.ps1")
}

# The editor writes the source project assets. CMake's POST_BUILD copy only runs
# when the executable is rebuilt, so always sync authored assets immediately
# before launch. This makes Edit -> Ctrl+S -> run-game.ps1 deterministic.
$SourceAssets = Join-Path $Root "examples\reference_game\assets"
$ExeDir = Split-Path -Parent $Exe
$RuntimeAssets = Join-Path $ExeDir "assets"

if (-not (Test-Path $SourceAssets)) {
    throw "Source assets folder not found: $SourceAssets"
}

if (Test-Path $RuntimeAssets) {
    Remove-Item $RuntimeAssets -Recurse -Force
}
Copy-Item $SourceAssets $RuntimeAssets -Recurse -Force
Write-Host "Synced reference-game assets from editor source -> runtime build." -ForegroundColor DarkGray

$SourceProject = Join-Path $Root "examples\reference_game\VesperaReference.vesperaproject"
$RuntimeProject = Join-Path $ExeDir "VesperaReference.vesperaproject"
Copy-Item $SourceProject $RuntimeProject -Force
Write-Host "Synced Vespera project workspace -> runtime build." -ForegroundColor DarkGray

$ManagedBuild = Join-Path $Root "build\managed\reference_game"
$RuntimeManaged = Join-Path $ExeDir "managed"
if (Test-Path $ManagedBuild) {
    if (Test-Path $RuntimeManaged) { Remove-Item $RuntimeManaged -Recurse -Force }
    Copy-Item $ManagedBuild $RuntimeManaged -Recurse -Force
    Write-Host "Synced Vespera.NET + C# game assemblies -> runtime build." -ForegroundColor DarkGray
} elseif (Test-Path $RuntimeManaged) {
    Remove-Item $RuntimeManaged -Recurse -Force
}

$ExeName = Split-Path -Leaf $Exe
Push-Location $ExeDir
try {
    & (Join-Path $ExeDir $ExeName)
} finally {
    Pop-Location
}
