param(
    [switch]$Automation,
    [int]$AutomationPort = 46787,
    [string]$Project = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $Root "build\editor\$Configuration\vespera_editor.exe"
if (-not $Project) { $Project = Join-Path $Root "examples\reference_game\VesperaReference.vesperaproject" }
elseif (-not [System.IO.Path]::IsPathRooted($Project)) { $Project = Join-Path (Get-Location) $Project }
$Project = [System.IO.Path]::GetFullPath($Project)

if (-not (Test-Path $Exe)) {
    Write-Host "Vespera Editor is not built yet. Running build.ps1 first..." -ForegroundColor Yellow
    & (Join-Path $Root "build.ps1") -Configuration $Configuration
}

$ExeDir = Split-Path -Parent $Exe
$EditorManaged = Join-Path $ExeDir "managed"

function Read-VesperaProjectSetting([string]$Path, [string]$Key) {
    if (-not (Test-Path $Path)) { return $null }
    $Pattern = '^' + [regex]::Escape($Key) + '\s+"(?<value>.*)"\s*$'
    foreach ($Line in Get-Content $Path) { if ($Line -match $Pattern) { return $Matches.value } }
    return $null
}

# 0.9.9f: every shipped starter project owns its managed sources. Build the selected project's
# assembly before editor launch so a fresh Hub-created project can enter Play
# immediately without borrowing reference-game metadata or requiring a manual
# Build C# step first.
$ManagedProjectSetting = Read-VesperaProjectSetting $Project "managed_project"
$ManagedAssembly = Read-VesperaProjectSetting $Project "managed_assembly"
if ($ManagedProjectSetting) {
    if (-not $ManagedAssembly) { throw "Project has managed_project but no managed_assembly: $Project" }
    $ProjectDir = Split-Path -Parent $Project
    $ManagedProjectPath = if ([System.IO.Path]::IsPathRooted($ManagedProjectSetting)) { $ManagedProjectSetting } else { Join-Path $ProjectDir $ManagedProjectSetting }
    $ManagedProjectPath = [System.IO.Path]::GetFullPath($ManagedProjectPath)
    if (-not (Test-Path $ManagedProjectPath)) { throw "Managed project not found: $ManagedProjectPath" }

    $HashInput = [Text.Encoding]::UTF8.GetBytes($Project.ToLowerInvariant())
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try { $ProjectHash = ([BitConverter]::ToString($Hasher.ComputeHash($HashInput))).Replace('-', '').Substring(0, 12).ToLowerInvariant() }
    finally { $Hasher.Dispose() }
    $SafeAssembly = $ManagedAssembly -replace '[^A-Za-z0-9._-]+', '-'
    $ManagedBuild = Join-Path $Root ("build\managed\editor-projects\{0}-{1}" -f $SafeAssembly, $ProjectHash)
    $Diagnostics = Join-Path $ManagedBuild "Vespera.ManagedBuildDiagnostics.txt"
    Write-Host "Building project C# scripts before editor launch..." -ForegroundColor Cyan
    & (Join-Path $Root "tools\build-managed-editor.ps1") -Project $ManagedProjectPath -GameAssemblyName $ManagedAssembly `
        -OutputDir $ManagedBuild -DiagnosticsFile $Diagnostics -NoMirrors
    if ($LASTEXITCODE -ne 0) { throw "Project C# build failed with exit code $LASTEXITCODE." }

    if (Test-Path $EditorManaged) { Remove-Item $EditorManaged -Recurse -Force }
    Copy-Item $ManagedBuild $EditorManaged -Recurse -Force
    Write-Host "Synced project C# script metadata -> Vespera Editor." -ForegroundColor DarkGray
} elseif (Test-Path $EditorManaged) {
    Remove-Item $EditorManaged -Recurse -Force
}

# The editor uses this helper for Build C#. Keeping the source-root knowledge in
# the launcher avoids baking sample-project paths into the editor executable.
$env:VESPERA_MANAGED_BUILD_HELPER = Join-Path $Root "tools\build-managed-editor.ps1"
$env:VESPERA_SOURCE_ROOT = $Root
$ExeName = Split-Path -Leaf $Exe
Push-Location $ExeDir
try {
    $EditorArgs = @($Project)
    if ($Automation) { $EditorArgs += "--automation-port=$AutomationPort" }
    & (Join-Path $ExeDir $ExeName) @EditorArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Vespera Editor exited with code $LASTEXITCODE." -ForegroundColor Red
        $Trace = Join-Path $ExeDir "vespera_editor_startup.log"
        if (Test-Path $Trace) {
            Write-Host "Startup trace: $Trace" -ForegroundColor Yellow
            Get-Content $Trace
        }
    }
} finally {
    Pop-Location
}
