param(
    [Parameter(Mandatory=$true)]
    [string]$Project,
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug",
    [ValidateSet("auto", "d3d12", "vulkan", "null")]
    [string]$Renderer = "auto",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not [System.IO.Path]::IsPathRooted($Project)) { $Project = Join-Path (Get-Location) $Project }
$Project = [System.IO.Path]::GetFullPath($Project)
if (-not (Test-Path $Project)) { throw "Vespera project not found: $Project" }

function Read-VesperaProjectSetting([string]$Path, [string]$Key) {
    $Pattern = '^' + [regex]::Escape($Key) + '\s+"(?<value>.*)"\s*$'
    foreach ($Line in Get-Content $Path) { if ($Line -match $Pattern) { return $Matches.value } }
    return $null
}

if (-not $NoBuild) { & (Join-Path $Root "build.ps1") -Configuration $Configuration }
$Player = Join-Path $Root "build\runtime\player\$Configuration\vespera_player.exe"
if (-not (Test-Path $Player)) { throw "Shared Vespera player is not built: $Player" }

$ManagedArgs = @()
$ManagedProject = Read-VesperaProjectSetting $Project "managed_project"
$ManagedAssembly = Read-VesperaProjectSetting $Project "managed_assembly"
if ($ManagedProject) {
    if (-not $ManagedAssembly) { throw "Project has managed_project but no managed_assembly." }
    $ProjectDir = Split-Path -Parent $Project
    if (-not [System.IO.Path]::IsPathRooted($ManagedProject)) { $ManagedProject = Join-Path $ProjectDir $ManagedProject }
    $ManagedProject = [System.IO.Path]::GetFullPath($ManagedProject)
    $SafeAssembly = $ManagedAssembly -replace '[^A-Za-z0-9._-]+', '-'
    $ManagedOut = Join-Path $Root "build\managed\player\$SafeAssembly"
    $ManagedConfiguration = if ($Configuration -eq "Debug") { "Debug" } else { "Release" }
    & (Join-Path $Root "tools\build-managed-editor.ps1") -Project $ManagedProject -GameAssemblyName $ManagedAssembly -Configuration $ManagedConfiguration `
        -OutputDir $ManagedOut -DiagnosticsFile (Join-Path $ManagedOut "Vespera.ManagedBuildDiagnostics.txt") -NoMirrors
    if ($LASTEXITCODE -ne 0) { throw "Project C# build failed with exit code $LASTEXITCODE." }
    $ManagedArgs = @("--managed-dir", $ManagedOut)
}

& $Player --project $Project --renderer $Renderer @ManagedArgs
exit $LASTEXITCODE
