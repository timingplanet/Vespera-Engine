param(
    [string]$Project,
    [ValidateSet("Debug", "Development", "Release")]
    [string]$Configuration = "Development",
    [string]$Output,
    [string]$ManagedDir,
    [ValidateSet("Project", "FrameworkDependent", "Portable")]
    [string]$ManagedDeployment = "Project",
    [string]$DotnetRoot,
    [switch]$Launch,
    [switch]$AllowReferenceGameFallback,
    [switch]$NoBuild,
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectWasExplicit = $PSBoundParameters.ContainsKey("Project") -and -not [string]::IsNullOrWhiteSpace($Project)
if (-not $ProjectWasExplicit) {
    if (-not $AllowReferenceGameFallback) {
        throw "-Project is required. Pass the .vesperaproject you want to package. For intentional QA/reference-game automation only, add -AllowReferenceGameFallback."
    }
    $Project = "examples\reference_game\VesperaReference.vesperaproject"
    Write-Warning "No -Project was supplied; -AllowReferenceGameFallback explicitly selected the Vespera reference QA game."
}
if (-not [System.IO.Path]::IsPathRooted($Project)) { $Project = Join-Path $Root $Project }
$Project = [System.IO.Path]::GetFullPath($Project)
if (-not (Test-Path $Project)) { throw "Vespera project not found: $Project" }
$ProjectDir = Split-Path -Parent $Project

function Read-VesperaProjectSetting([string]$Path, [string]$Key) {
    $Pattern = '^' + [regex]::Escape($Key) + '\s+"(?<value>.*)"\s*$'
    foreach ($Line in Get-Content $Path) {
        if ($Line -match $Pattern) { return $Matches.value }
    }
    return $null
}

function Read-VesperaProjectRawSetting([string]$Path, [string]$Key) {
    $Pattern = '^' + [regex]::Escape($Key) + '\s+(?<value>\S+)\s*$'
    foreach ($Line in Get-Content $Path) {
        if ($Line -match $Pattern) { return $Matches.value }
    }
    return $null
}

function Find-CMake {
    $OnPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($OnPath) { return $OnPath.Source }
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWhere) {
        foreach ($Installation in (& $VsWhere -products * -all -property installationPath)) {
            if (-not $Installation) { continue }
            $Candidate = Join-Path $Installation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $Candidate) { return $Candidate }
        }
    }
    foreach ($Candidate in @("$env:ProgramFiles\CMake\bin\cmake.exe", "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe")) {
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }
    return $null
}

$ProjectName = Read-VesperaProjectSetting $Project "name"
if (-not $ProjectName) { $ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($Project) }
$GameTarget = Read-VesperaProjectSetting $Project "game_target"
$UsesSharedPlayer = -not $GameTarget
if ($UsesSharedPlayer) { $GameTarget = "vespera_player" }
$ManagedProject = Read-VesperaProjectSetting $Project "managed_project"
$ManagedAssembly = Read-VesperaProjectSetting $Project "managed_assembly"
$ProjectDeployment = Read-VesperaProjectSetting $Project "managed_deployment"
if (-not $ProjectDeployment) { $ProjectDeployment = "framework-dependent" }
$Deployment = if ($ManagedDeployment -eq "Project") { $ProjectDeployment } elseif ($ManagedDeployment -eq "Portable") { "portable" } else { "framework-dependent" }
$ExecutableName = Read-VesperaProjectSetting $Project "executable_name"
if (-not $ExecutableName) { $ExecutableName = ($ProjectName -replace '[^A-Za-z0-9._-]+', '-') }
$BuildOutputSetting = Read-VesperaProjectSetting $Project "build_output_directory"
if (-not $BuildOutputSetting) { $BuildOutputSetting = "builds" }
$DevelopmentDiagnosticsRaw = Read-VesperaProjectRawSetting $Project "development_diagnostics"
$DevelopmentDiagnostics = if ($null -eq $DevelopmentDiagnosticsRaw) { $true } else { $DevelopmentDiagnosticsRaw -ne "0" }

$NativeConfig = switch ($Configuration) {
    "Release" { "Release" }
    "Development" { "RelWithDebInfo" }
    default { "Debug" }
}
$IncludeDebugSymbols = $Configuration -eq "Debug" -or ($Configuration -eq "Development" -and $DevelopmentDiagnostics)
$BuildDir = Join-Path $Root "build"

if (-not $NoBuild) {
    # Export should not rebuild the entire editor/reference checkout every time.
    # Reuse the configured multi-config tree and build only the runtime target +
    # packager needed by this project. A first-ever source checkout still falls
    # back to build.ps1 once so dependencies/toolchain configuration are created.
    $CacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if (-not (Test-Path $CacheFile)) {
        & (Join-Path $Root "build.ps1") -Configuration Debug
        if ($LASTEXITCODE -ne 0) { throw "Vespera initial build failed before export." }
    }

    $CMake = Find-CMake
    if (-not $CMake) { throw "CMake could not be located for $Configuration export." }
    Write-Host "`nBuilding required native export targets..." -ForegroundColor Cyan
    & $CMake --build $BuildDir --config $NativeConfig --target $GameTarget vespera_packager
    if ($LASTEXITCODE -ne 0) { throw "$Configuration export targets failed with exit code $LASTEXITCODE." }
}

$Packager = Join-Path $BuildDir "tools\packager\$NativeConfig\vespera_packager.exe"
if (-not (Test-Path $Packager)) {
    throw "Vespera packager is not built for $NativeConfig`: $Packager. Run without -NoBuild first."
}

if ($UsesSharedPlayer) {
    $RuntimeExe = Join-Path $BuildDir "runtime\player\$NativeConfig\vespera_player.exe"
    if (-not (Test-Path $RuntimeExe -PathType Leaf)) {
        throw "Shared player was built but its expected $NativeConfig runtime is missing: $RuntimeExe"
    }
} else {
    $RuntimeCandidates = @(Get-ChildItem -Path $BuildDir -Filter "$GameTarget.exe" -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match [regex]::Escape("\$NativeConfig\") })
    if ($RuntimeCandidates.Count -eq 0) {
        throw "Could not find built runtime target '$GameTarget.exe' for $NativeConfig under $BuildDir."
    }
    if ($RuntimeCandidates.Count -gt 1) {
        $CandidateText = ($RuntimeCandidates | ForEach-Object { "  - $($_.FullName)" }) -join "`n"
        throw "Runtime target '$GameTarget.exe' is ambiguous for $NativeConfig. Refusing to package the first recursive match.`n$CandidateText"
    }
    $RuntimeExe = $RuntimeCandidates[0].FullName
}

if (-not $ManagedDir -and $ManagedProject) {
    if (-not $ManagedAssembly) { throw "Project has managed_project but no managed_assembly." }
    $ManagedProjectPath = if ([System.IO.Path]::IsPathRooted($ManagedProject)) { $ManagedProject } else { Join-Path $ProjectDir $ManagedProject }
    $ManagedProjectPath = [System.IO.Path]::GetFullPath($ManagedProjectPath)
    if (-not (Test-Path $ManagedProjectPath)) { throw "Managed project not found: $ManagedProjectPath" }
    $HashInput = [Text.Encoding]::UTF8.GetBytes($Project.ToLowerInvariant())
    $Hasher = [Security.Cryptography.SHA256]::Create()
    try { $ProjectHash = ([BitConverter]::ToString($Hasher.ComputeHash($HashInput))).Replace('-', '').Substring(0, 12).ToLowerInvariant() }
    finally { $Hasher.Dispose() }
    $ManagedDir = Join-Path $BuildDir ("managed\exports\{0}-{1}" -f (($ManagedAssembly -replace '[^A-Za-z0-9._-]+', '-')), $ProjectHash)
    $ManagedDiagnostics = Join-Path $ManagedDir "Vespera.ManagedBuildDiagnostics.txt"
    Write-Host "`nBuilding project C# assembly for export..." -ForegroundColor Cyan
    & (Join-Path $Root "tools\build-managed-editor.ps1") -Project $ManagedProjectPath -GameAssemblyName $ManagedAssembly `
        -OutputDir $ManagedDir -DiagnosticsFile $ManagedDiagnostics -NoMirrors
    if ($LASTEXITCODE -ne 0) { throw "Project C# build failed before export with exit code $LASTEXITCODE." }
}

if (-not $ManagedDir -and $ManagedAssembly) {
    $ManagedRoot = Join-Path $BuildDir "managed"
    $ManagedCandidates = @()
    if (Test-Path $ManagedRoot -PathType Container) {
        $ManagedCandidates = @(Get-ChildItem -Path $ManagedRoot -Filter "$ManagedAssembly.dll" -Recurse -File -ErrorAction SilentlyContinue)
    }
    if ($ManagedCandidates.Count -gt 1) {
        $CandidateText = ($ManagedCandidates | ForEach-Object { "  - $($_.FullName)" }) -join "`n"
        throw "Managed assembly '$ManagedAssembly.dll' has multiple staged candidates and the project declares no managed_project. Refusing to package an arbitrary stale stage.`n$CandidateText"
    }
    if ($ManagedCandidates.Count -eq 1) { $ManagedDir = $ManagedCandidates[0].DirectoryName }
}

if (-not $Output) {
    $SafeName = ($ProjectName -replace '[^A-Za-z0-9._-]+', '-')
    $BuildOutputRoot = if ([System.IO.Path]::IsPathRooted($BuildOutputSetting)) {
        $BuildOutputSetting
    } else {
        Join-Path $ProjectDir $BuildOutputSetting
    }
    $Output = Join-Path $BuildOutputRoot ("{0}-{1}" -f $SafeName, $Configuration)
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $ProjectDir $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

$Args = @(
    "--project", $Project,
    "--output", $Output,
    "--runtime", $RuntimeExe,
    "--configuration", $Configuration,
    "--managed-deployment", $Deployment
)
if ($ManagedDir) { $Args += @("--managed", $ManagedDir) }
if ($DotnetRoot) { $Args += @("--dotnet-root", $DotnetRoot) }
if ($NoClean) { $Args += "--no-clean" }
if ($IncludeDebugSymbols) { $Args += "--debug-symbols" } else { $Args += "--no-debug-symbols" }

Write-Host "Vespera Engine 1.0.0 export" -ForegroundColor Cyan
Write-Host "Project:       $Project"
Write-Host "Configuration: $Configuration ($NativeConfig native)"
Write-Host "Runtime:       $RuntimeExe"
if ($UsesSharedPlayer) { Write-Host "Runtime model: shared vespera_player (project has no custom game_target)" -ForegroundColor DarkCyan }
Write-Host "Executable:    $ExecutableName"
Write-Host "Managed mode:  $Deployment"
Write-Host "Diagnostics:   $DevelopmentDiagnostics"
if ($ManagedDir) { Write-Host "Managed:       $ManagedDir" }
Write-Host "Output:        $Output"

& $Packager @Args
if ($LASTEXITCODE -ne 0) { throw "Vespera package export failed with exit code $LASTEXITCODE." }

Write-Host "`nExport succeeded." -ForegroundColor Green
Write-Host "Package:  $Output"
Write-Host "Manifest: $(Join-Path $Output 'Vespera.PackageManifest.txt')"
Write-Host "Report:   $(Join-Path $Output 'Vespera.PackageReport.txt')"

if ($Launch) {
    $RuntimeName = $ExecutableName
    if (-not [System.IO.Path]::HasExtension($RuntimeName)) { $RuntimeName += ".exe" }
    $PackagedRuntime = Join-Path $Output $RuntimeName
    if (-not (Test-Path $PackagedRuntime)) {
        $FallbackRuntime = @(Get-ChildItem -Path $Output -Filter "*.exe" -File -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($FallbackRuntime.Count -gt 0) { $PackagedRuntime = $FallbackRuntime[0].FullName }
    }
    if (-not (Test-Path $PackagedRuntime)) { throw "Export succeeded but packaged runtime could not be located for launch." }
    Write-Host "Launching: $PackagedRuntime" -ForegroundColor Cyan
    Start-Process -FilePath $PackagedRuntime -WorkingDirectory $Output
}
