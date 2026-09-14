param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$ManagedHelper = Join-Path $Root "tools\build-managed-editor.ps1"
if (-not (Test-Path -LiteralPath $ManagedHelper -PathType Leaf)) {
    throw "Required managed build helper is missing from this Vespera source package: $ManagedHelper"
}

$ProjectFile = Join-Path $Root "examples\reference_game\VesperaReference.vesperaproject"
function Read-VesperaProjectSetting([string]$Path, [string]$Key) {
    if (-not (Test-Path $Path)) { return $null }
    $Pattern = '^' + [regex]::Escape($Key) + '\s+"(?<value>.*)"\s*$'
    foreach ($Line in Get-Content $Path) {
        if ($Line -match $Pattern) { return $Matches.value }
    }
    return $null
}
$ManagedProjectSetting = Read-VesperaProjectSetting $ProjectFile "managed_project"
$ManagedAssemblySetting = Read-VesperaProjectSetting $ProjectFile "managed_assembly"
$ProjectRoot = Split-Path -Parent $ProjectFile
$ManagedProjectPath = if ($ManagedProjectSetting) { Join-Path $ProjectRoot $ManagedProjectSetting } else { Join-Path $Root "examples\reference_game\managed\ReferenceGame.Scripts.csproj" }
$ManagedAssemblyName = if ($ManagedAssemblySetting) { $ManagedAssemblySetting } else { [System.IO.Path]::GetFileNameWithoutExtension($ManagedProjectPath) }

Write-Host "Vespera Engine build" -ForegroundColor Cyan
Write-Host "Root: $Root"

function Find-CMake {
    $OnPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($OnPath) {
        return $OnPath.Source
    }

    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWhere) {
        $Installations = & $VsWhere -products * -all -property installationPath
        foreach ($Installation in $Installations) {
            if (-not $Installation) { continue }
            $Candidate = Join-Path $Installation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $Candidate) {
                return $Candidate
            }
        }
    }

    $FallbackRoots = @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
    )
    foreach ($Candidate in $FallbackRoots) {
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }

    return $null
}

function Find-Dotnet {
    $OnPath = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($OnPath) { return $OnPath.Source }

    $Candidates = @(
        "$env:ProgramW6432\dotnet\dotnet.exe",
        "$env:ProgramFiles\dotnet\dotnet.exe",
        "${env:ProgramFiles(x86)}\dotnet\dotnet.exe"
    )
    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }
    return $null
}

$CMake = Find-CMake
if (-not $CMake) {
    throw "CMake could not be located. Install Visual Studio's 'C++ CMake tools for Windows' component or a current standalone CMake release."
}

$VersionText = (& $CMake --version | Select-Object -First 1)
Write-Host "CMake: $VersionText" -ForegroundColor DarkCyan
Write-Host "CMake path: $CMake" -ForegroundColor DarkGray

$CMakeHelp = (& $CMake --help | Out-String)
$Generator = $null

if ($CMakeHelp -match [regex]::Escape("Visual Studio 18 2026")) {
    $Generator = "Visual Studio 18 2026"
} elseif ($CMakeHelp -match [regex]::Escape("Visual Studio 17 2022")) {
    $Generator = "Visual Studio 17 2022"
}

if (-not $Generator) {
    throw "No supported Visual Studio CMake generator was found. Vespera currently supports Visual Studio 2026 or 2022. VS 2026 requires CMake 4.2 or newer. Detected: $VersionText"
}

Write-Host "Generator: $Generator" -ForegroundColor DarkCyan

$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CacheFile) {
    $CachedGeneratorLine = Select-String -Path $CacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | Select-Object -First 1
    if ($CachedGeneratorLine) {
        $CachedGenerator = $CachedGeneratorLine.Matches[0].Groups[1].Value
        if ($CachedGenerator -ne $Generator) {
            Write-Host "Build directory was generated with '$CachedGenerator'. Recreating it for '$Generator'..." -ForegroundColor Yellow
            Remove-Item -Recurse -Force $BuildDir
        }
    }
}

& $CMake -S $Root -B $BuildDir -G $Generator -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }

& $CMake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Vespera native build failed with exit code $LASTEXITCODE." }


# Build Vespera.NET and the reference game's C# assembly when .NET is available.
# The helper stages outputs and only replaces the last-good managed build after
# compilation + reflection metadata generation both succeed.
$Dotnet = Find-Dotnet
if ($Dotnet) {
    $ManagedOut = Join-Path $BuildDir "managed\reference_game"
    $EditorManaged = Join-Path $BuildDir "editor\$Configuration\managed"
    $Diagnostics = Join-Path $EditorManaged "Vespera.ManagedBuildDiagnostics.txt"
    Write-Host "`nBuilding Vespera C# scripts with last-good protection..." -ForegroundColor Cyan
    & $ManagedHelper -Project $ManagedProjectPath -GameAssemblyName $ManagedAssemblyName `
        -OutputDir $ManagedOut -EditorManagedDir $EditorManaged -DiagnosticsFile $Diagnostics -AllowUnavailable
    if ($LASTEXITCODE -ne 0) {
        throw "Vespera managed build failed with exit code $LASTEXITCODE. Previous good managed output was preserved."
    }
    if (Test-Path $ManagedOut) { Write-Host "Managed: $ManagedOut" -ForegroundColor DarkCyan }
} else {
    Write-Warning "dotnet SDK not found. Native build is usable; C# scripting will be unavailable until .NET 8+ is installed."
}

$EditorExe = Join-Path $BuildDir "editor\$Configuration\vespera_editor.exe"
$GameExe = Join-Path $BuildDir "examples\reference_game\$Configuration\vespera_reference_game.exe"
$HubExe = Join-Path $BuildDir "examples\project_hub\$Configuration\vespera_project_hub.exe"
$PlayerExe = Join-Path $BuildDir "runtime\player\$Configuration\vespera_player.exe"

if ((Test-Path $EditorExe) -and (Test-Path $GameExe) -and (Test-Path $HubExe) -and (Test-Path $PlayerExe)) {
    Write-Host "`nBuild succeeded." -ForegroundColor Green
    Write-Host "Editor: $EditorExe"
    Write-Host "Game:   $GameExe"
    Write-Host "Hub:    $HubExe"
    Write-Host "Player: $PlayerExe"
    Write-Host "`nOpen the Vespera Project Hub with:" -ForegroundColor Cyan
    Write-Host ".\vespera.ps1"
    Write-Host "`nRun the reference editor project directly with:" -ForegroundColor Cyan
    Write-Host ".\run.ps1"
    Write-Host "`nRun the reference game with:" -ForegroundColor Cyan
    Write-Host ".\run-game.ps1"
    Write-Host "`nExport a Development package with:" -ForegroundColor Cyan
    Write-Host ".\export.ps1 -Project <path-to-.vesperaproject>"
} else {
    if (-not (Test-Path $EditorExe)) { Write-Warning "Expected editor executable was not found: $EditorExe" }
    if (-not (Test-Path $GameExe)) { Write-Warning "Expected reference game executable was not found: $GameExe" }
    if (-not (Test-Path $HubExe)) { Write-Warning "Expected Project Hub executable was not found: $HubExe" }
    if (-not (Test-Path $PlayerExe)) { Write-Warning "Expected shared player executable was not found: $PlayerExe" }
}
