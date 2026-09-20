param(
    [string]$Project,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$OutputDir,
    [string]$EditorManagedDir,
    [string]$RuntimeManagedDir,
    [string]$DiagnosticsFile,
    [string]$GameAssemblyName,
    [switch]$AllowUnavailable,
    [switch]$NoMirrors
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Project) { $Project = Join-Path $Root "examples\reference_game\managed\ReferenceGame.Scripts.csproj" }
if (-not $GameAssemblyName) { $GameAssemblyName = [System.IO.Path]::GetFileNameWithoutExtension($Project) }
if (-not $OutputDir) { $OutputDir = Join-Path $Root "build\managed\reference_game" }
if (-not $NoMirrors) {
    if (-not $EditorManagedDir) { $EditorManagedDir = Join-Path $Root "build\editor\$Configuration\managed" }
    if (-not $RuntimeManagedDir) { $RuntimeManagedDir = Join-Path $Root "build\examples\reference_game\$Configuration\managed" }
} else {
    $EditorManagedDir = ""
    $RuntimeManagedDir = ""
}
if (-not $DiagnosticsFile) {
    $DiagnosticsFile = if ($EditorManagedDir) { Join-Path $EditorManagedDir "Vespera.ManagedBuildDiagnostics.txt" } else { Join-Path $OutputDir "Vespera.ManagedBuildDiagnostics.txt" }
}

function Find-Dotnet {
    $OnPath = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($OnPath) { return $OnPath.Source }
    foreach ($Candidate in @(
        "$env:ProgramW6432\dotnet\dotnet.exe",
        "$env:ProgramFiles\dotnet\dotnet.exe",
        "${env:ProgramFiles(x86)}\dotnet\dotnet.exe"
    )) {
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }
    return $null
}

function Quote-Record([string]$Text) {
    if ($null -eq $Text) { $Text = "" }
    return '"' + $Text.Replace('\', '\\').Replace('"', '\"').Replace("`r", '\r').Replace("`n", '\n').Replace("`t", '\t') + '"'
}

function Copy-AtomicFile([string]$Source, [string]$Destination) {
    $Parent = Split-Path -Parent $Destination
    if ($Parent) { New-Item -ItemType Directory -Force -Path $Parent | Out-Null }

    # Keep the temporary and backup files beside the destination so File.Replace
    # stays on the same volume and can perform an atomic swap. Windows PowerShell
    # / .NET Framework does not reliably accept $null for File.Replace's backup
    # path, so always provide a real temporary backup name and remove it afterward.
    $Temp = "$Destination.vespera-new.$PID"
    $Backup = "$Destination.vespera-old.$PID"
    Remove-Item $Temp -Force -ErrorAction SilentlyContinue
    Remove-Item $Backup -Force -ErrorAction SilentlyContinue
    Copy-Item $Source $Temp -Force

    try {
        if (Test-Path $Destination) {
            [System.IO.File]::Replace($Temp, $Destination, $Backup)
            Remove-Item $Backup -Force -ErrorAction SilentlyContinue
        } else {
            Move-Item $Temp $Destination
        }
    } catch {
        Remove-Item $Temp -Force -ErrorAction SilentlyContinue
        # If Replace created a backup before failing, restore it only when the
        # destination disappeared; otherwise leave the original destination alone.
        if ((-not (Test-Path $Destination)) -and (Test-Path $Backup)) {
            Move-Item $Backup $Destination -Force
        }
        Remove-Item $Backup -Force -ErrorAction SilentlyContinue
        throw
    }
}

function Write-Diagnostics([string]$Status, [string[]]$Lines, [string]$Tfm = "") {
    $Parent = Split-Path -Parent $DiagnosticsFile
    if ($Parent) { New-Item -ItemType Directory -Force -Path $Parent | Out-Null }
    $Records = [System.Collections.Generic.List[string]]::new()
    $Records.Add("vespera_managed_build 1")
    $Records.Add("status $(Quote-Record $Status)")
    $Records.Add("tfm $(Quote-Record $Tfm)")
    foreach ($Line in $Lines) {
        $Text = [string]$Line
        if ($Text -match '^(?<file>.+)\((?<line>\d+),(?<col>\d+)(?:,\d+,\d+)?\):\s+(?<severity>error|warning)\s+(?<code>[A-Za-z]+\d+):\s+(?<message>.*?)(?:\s+\[[^\]]+\])?$') {
            $Records.Add("diagnostic $(Quote-Record $Matches.severity) $(Quote-Record $Matches.code) $(Quote-Record $Matches.file) $($Matches.line) $($Matches.col) $(Quote-Record $Matches.message)")
        } elseif ($Text -match '^(?:.*?:\s+)?(?<severity>error|warning)\s+(?<code>[A-Za-z]+\d+):\s+(?<message>.*?)(?:\s+\[[^\]]+\])?$') {
            $Records.Add("diagnostic $(Quote-Record $Matches.severity) $(Quote-Record $Matches.code) $(Quote-Record '') 0 0 $(Quote-Record $Matches.message)")
        }
    }
    foreach ($Line in ($Lines | Select-Object -Last 16)) {
        if ([string]::IsNullOrWhiteSpace([string]$Line)) { continue }
        $Records.Add("output $(Quote-Record ([string]$Line))")
    }
    $Records.Add("end_build")
    Set-Content -Path $DiagnosticsFile -Value $Records -Encoding UTF8
}

$Dotnet = Find-Dotnet
if (-not $Dotnet) {
    $State = if ($AllowUnavailable) { "unavailable" } else { "failed" }
    Write-Diagnostics $State @("dotnet SDK not found. Install a .NET 8+ x64 SDK or add dotnet to PATH.")
    if ($AllowUnavailable) { Write-Warning "dotnet SDK not found; native build remains usable."; exit 0 }
    exit 10
}
if (-not (Test-Path $Project)) {
    Write-Diagnostics "failed" @("Managed project not found: $Project")
    exit 11
}

$SdkLines = & $Dotnet --list-sdks
$StableSdkMajors = @()
$AnySdkMajors = @()
foreach ($Line in $SdkLines) {
    if ($Line -match '^(\d+)\.([^\s]+)') {
        $Major = [int]$Matches[1]
        if ($Major -ge 8) {
            $AnySdkMajors += $Major
            if ($Line -notmatch '-(preview|rc)') { $StableSdkMajors += $Major }
        }
    }
}
$Candidates = if ($StableSdkMajors.Count -gt 0) { $StableSdkMajors } else { $AnySdkMajors }
$ManagedMajor = ($Candidates | Measure-Object -Maximum).Maximum
if (-not $ManagedMajor) {
    $State = if ($AllowUnavailable) { "unavailable" } else { "failed" }
    Write-Diagnostics $State @("dotnet was found, but no .NET 8+ SDK is installed.")
    if ($AllowUnavailable) { Write-Warning "No .NET 8+ SDK is installed; native build remains usable."; exit 0 }
    exit 12
}
$Tfm = "net$ManagedMajor.0"

$Stage = "$OutputDir.staging.$PID"
$ToolStage = Join-Path $Stage "_tools"
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

$VesperaSdkProject = Join-Path $Root "managed\Vespera.NET\Vespera.NET.csproj"
$VesperaSdkArgument = "-p:VesperaSdkProject=$VesperaSdkProject"
$BuildLines = @(& $Dotnet build $Project -c $Configuration -p:VesperaTargetFramework=$Tfm $VesperaSdkArgument -o $Stage --nologo -v:minimal 2>&1 | ForEach-Object { $_.ToString() })
$BuildExit = $LASTEXITCODE
if ($BuildExit -ne 0) {
    Write-Diagnostics "failed" $BuildLines $Tfm
    Write-Host "Managed compiler diagnostics:" -ForegroundColor Red
    foreach ($Line in $BuildLines) { Write-Host $Line }
    Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
    exit $BuildExit
}

$GameAssemblyFile = "$GameAssemblyName.dll"
foreach ($Required in @("Vespera.NET.dll", $GameAssemblyFile)) {
    if (-not (Test-Path (Join-Path $Stage $Required))) {
        $BuildLines += "Managed build succeeded but expected output is missing: $Required"
        Write-Diagnostics "failed" $BuildLines $Tfm
        Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
        exit 13
    }
}

New-Item -ItemType Directory -Force -Path $ToolStage | Out-Null
$ToolLines = @(& $Dotnet build (Join-Path $Root "managed\Vespera.ScriptTool\Vespera.ScriptTool.csproj") -c $Configuration -p:VesperaTargetFramework=$Tfm -o $ToolStage --nologo -v:minimal 2>&1 | ForEach-Object { $_.ToString() })
$ToolExit = $LASTEXITCODE
$BuildLines += $ToolLines
if ($ToolExit -ne 0) {
    Write-Diagnostics "failed" $BuildLines $Tfm
    Write-Host "Managed script-tool compiler diagnostics:" -ForegroundColor Red
    foreach ($Line in $ToolLines) { Write-Host $Line }
    Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
    exit $ToolExit
}

$ScriptTool = Join-Path $ToolStage "Vespera.ScriptTool.dll"
$Metadata = Join-Path $Stage "Vespera.ScriptMetadata.txt"
$MetadataLines = @(& $Dotnet $ScriptTool (Join-Path $Stage $GameAssemblyFile) $Metadata 2>&1 | ForEach-Object { $_.ToString() })
$MetadataExit = $LASTEXITCODE
$BuildLines += $MetadataLines
if ($MetadataExit -ne 0 -or -not (Test-Path $Metadata)) {
    Write-Diagnostics "failed" $BuildLines $Tfm
    Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
    $FailureCode = if ($MetadataExit -ne 0) { $MetadataExit } else { 14 }
    exit $FailureCode
}
Remove-Item $ToolStage -Recurse -Force

$RuntimeVersions = @()
foreach ($Line in (& $Dotnet --list-runtimes)) {
    if ($Line -match '^Microsoft\.NETCore\.App\s+(\d+\.[^\s]+)\s+') {
        $Version = $Matches[1]
        if ($Version -match "^$ManagedMajor\.") { $RuntimeVersions += $Version }
    }
}
$RuntimeVersion = if ($RuntimeVersions.Count -gt 0) { $RuntimeVersions[-1] } else { "$ManagedMajor.0.0" }
$RuntimeConfig = @{
    runtimeOptions = @{
        tfm = $Tfm
        framework = @{ name = "Microsoft.NETCore.App"; version = $RuntimeVersion }
        rollForward = "LatestMajor"
    }
} | ConvertTo-Json -Depth 4
Set-Content -Path (Join-Path $Stage "Vespera.Managed.runtimeconfig.json") -Value $RuntimeConfig -Encoding UTF8

# Last-good protection: the existing output is untouched until all build/reflection steps succeed.
$Backup = "$OutputDir.previous.$PID"
if (Test-Path $Backup) { Remove-Item $Backup -Recurse -Force }
try {
    if (Test-Path $OutputDir) { Move-Item $OutputDir $Backup }
    Move-Item $Stage $OutputDir
    if (Test-Path $Backup) { Remove-Item $Backup -Recurse -Force }
} catch {
    if (Test-Path $OutputDir) { Remove-Item $OutputDir -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path $Backup) { Move-Item $Backup $OutputDir }
    throw
}

# 0.8.7: Editor Play Mode now hosts Vespera.NET and project C# in-process.
# Treat the editor mirror exactly like a live runtime mirror: Vespera.NET is a
# process-lifetime bridge and must not be replaced underneath a running editor,
# while the collectible game assembly can be committed atomically for reload.
if ($EditorManagedDir) {
    New-Item -ItemType Directory -Force -Path $EditorManagedDir | Out-Null
    $BuiltBridge = Join-Path $OutputDir "Vespera.NET.dll"
    $EditorBridge = Join-Path $EditorManagedDir "Vespera.NET.dll"
    $EditorBridgeChanged = $false
    if ((Test-Path $BuiltBridge) -and (Test-Path $EditorBridge)) {
        $BuiltHash = (Get-FileHash $BuiltBridge -Algorithm SHA256).Hash
        $EditorHash = (Get-FileHash $EditorBridge -Algorithm SHA256).Hash
        $EditorBridgeChanged = $BuiltHash -ne $EditorHash
    }

    if ($EditorBridgeChanged) {
        $BuildLines += "Vespera.NET changed since the editor mirror was created; restart Vespera Editor before using the new bridge."
        Write-Warning "Vespera.NET changed. C# build succeeded, but Vespera Editor must be restarted before using the new bridge."
    } else {
        # Seed the bridge/runtime config on first build, but never replace a live bridge.
        foreach ($FixedName in @("Vespera.NET.dll", "Vespera.NET.pdb", "Vespera.Managed.runtimeconfig.json")) {
            $SourceFixed = Join-Path $OutputDir $FixedName
            $DestFixed = Join-Path $EditorManagedDir $FixedName
            if ((Test-Path $SourceFixed) -and -not (Test-Path $DestFixed)) {
                Copy-Item $SourceFixed $DestFixed -Force
            }
        }
        $EditorReloadable = @(Get-ChildItem $OutputDir -File | Where-Object {
            $_.Name -notin @("Vespera.NET.dll", "Vespera.NET.pdb", "Vespera.Managed.runtimeconfig.json")
        })
        foreach ($File in ($EditorReloadable | Where-Object { $_.Name -ne $GameAssemblyFile })) {
            Copy-AtomicFile $File.FullName (Join-Path $EditorManagedDir $File.Name)
        }
        $EditorGameDll = $EditorReloadable | Where-Object { $_.Name -eq $GameAssemblyFile } | Select-Object -First 1
        if ($EditorGameDll) {
            Copy-AtomicFile $EditorGameDll.FullName (Join-Path $EditorManagedDir $EditorGameDll.Name)
            $BuildLines += "Editor Play mirror committed $GameAssemblyFile last for debounced automatic reload."
        }
    }
}

# The reference game loads project game code/dependencies through a collectible
# context, so those files may be refreshed while the game is running. Do NOT
# replace Vespera.NET.dll here: the bridge assembly is process-lifetime in the
# default context and changes to it intentionally require restarting the game.
if ($RuntimeManagedDir) {
    New-Item -ItemType Directory -Force -Path $RuntimeManagedDir | Out-Null
    $BuiltBridge = Join-Path $OutputDir "Vespera.NET.dll"
    $RuntimeBridge = Join-Path $RuntimeManagedDir "Vespera.NET.dll"
    $BridgeChanged = $false
    if ((Test-Path $BuiltBridge) -and (Test-Path $RuntimeBridge)) {
        $BuiltHash = (Get-FileHash $BuiltBridge -Algorithm SHA256).Hash
        $RuntimeHash = (Get-FileHash $RuntimeBridge -Algorithm SHA256).Hash
        $BridgeChanged = $BuiltHash -ne $RuntimeHash
    }

    if ($BridgeChanged) {
        $BuildLines += "Vespera.NET changed since the running-game mirror was created; restart the reference game before loading this C# build."
        Write-Warning "Vespera.NET changed. Build succeeded, but a running reference game must be restarted before using the new scripts."
    } else {
        # Copy dependencies/PDB/metadata first, then commit the game DLL last.
        # The runtime watches ReferenceGame.Scripts.dll as the replacement marker,
        # so it can never observe the DLL before its companion files are ready.
        $Reloadable = @(Get-ChildItem $OutputDir -File | Where-Object {
            $_.Name -notin @("Vespera.NET.dll", "Vespera.NET.pdb", "Vespera.Managed.runtimeconfig.json")
        })
        foreach ($File in ($Reloadable | Where-Object { $_.Name -ne $GameAssemblyFile })) {
            Copy-AtomicFile $File.FullName (Join-Path $RuntimeManagedDir $File.Name)
        }
        $GameDll = $Reloadable | Where-Object { $_.Name -eq $GameAssemblyFile } | Select-Object -First 1
        if ($GameDll) {
            Copy-AtomicFile $GameDll.FullName (Join-Path $RuntimeManagedDir $GameDll.Name)
            $BuildLines += "Runtime managed mirror committed $GameAssemblyFile last for debounced automatic reload."
        }
    }
}

$BuildLines += "Managed build succeeded; last-good output and editor/runtime mirrors refreshed. Running editor Play Mode and the reference game can automatically reload the committed game assembly."
Write-Diagnostics "success" $BuildLines $Tfm
Write-Host "Vespera C# build succeeded ($Tfm)." -ForegroundColor Green
exit 0
