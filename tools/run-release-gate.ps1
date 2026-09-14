param(
    [switch]$NoBuild,
    [switch]$SkipTests,
    [switch]$KeepWorkDirectory,
    [int]$LaunchSeconds = 6
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$Work = Join-Path ([System.IO.Path]::GetTempPath()) "Vespera-ReleaseGate-$Stamp-$PID"
$Projects = Join-Path $Work "projects"
$Exports = Join-Path $Work "exports"
New-Item -ItemType Directory -Force -Path $Projects, $Exports | Out-Null

function Invoke-Step([string]$Title, [scriptblock]$Body) {
    Write-Host "`n== $Title ==" -ForegroundColor Cyan
    & $Body
}

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
}

function Assert-Directory([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description is missing: $Path"
    }
}

function Test-SourcePackagePreflight {
    $RequiredFiles = @(
        "build.ps1",
        "test.ps1",
        "export.ps1",
        "tools\build-managed-editor.ps1",
        "tools\validate_source.py",
        "examples\project_hub\CMakeLists.txt",
        "runtime\player\CMakeLists.txt",
        "managed\Vespera.NET\Vespera.NET.csproj"
    )
    foreach ($Relative in $RequiredFiles) {
        Assert-File (Join-Path $Root $Relative) "Release-gate source requirement '$Relative'"
    }
}

function Get-SingleFile([string]$Directory, [string]$Filter, [string]$Description) {
    $Files = @(Get-ChildItem -LiteralPath $Directory -Filter $Filter -File -ErrorAction Stop)
    if ($Files.Count -ne 1) {
        throw "$Description expected exactly one '$Filter' in $Directory, found $($Files.Count)."
    }
    return $Files[0].FullName
}

function Get-PeSubsystem([string]$Executable) {
    $Stream = [System.IO.File]::Open($Executable, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $Reader = [System.IO.BinaryReader]::new($Stream)
        try {
            if ($Reader.ReadUInt16() -ne 0x5A4D) { throw "Not a Windows PE executable: $Executable" }
            $Stream.Position = 0x3C
            $PeOffset = $Reader.ReadUInt32()
            $Stream.Position = $PeOffset
            if ($Reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Executable" }
            $Stream.Position = $PeOffset + 4 + 20 + 68
            return $Reader.ReadUInt16()
        } finally {
            $Reader.Dispose()
        }
    } finally {
        $Stream.Dispose()
    }
}

function Test-Package([string]$Directory, [bool]$RequirePortable) {
    Assert-Directory $Directory "Package directory"
    $ProjectFile = Get-SingleFile $Directory "*.vesperaproject" "Package project"
    $Runtime = Get-SingleFile $Directory "*.exe" "Package runtime"
    $Subsystem = Get-PeSubsystem $Runtime
    if ($Subsystem -ne 2) {
        throw "Release package runtime should use IMAGE_SUBSYSTEM_WINDOWS_GUI (2), found $Subsystem`: $Runtime"
    }
    Assert-File (Join-Path $Directory "Vespera.PackageManifest.txt") "Package manifest"
    Assert-File (Join-Path $Directory "Vespera.PackageReport.txt") "Package report"
    Assert-File (Join-Path $Directory "Vespera.BuildAssetIndex.txt") "Build asset index"
    Assert-File (Join-Path $Directory "branding\vespera_icon_window.png") "Runtime fallback icon"
    Assert-File (Join-Path $Directory "branding\vespera_splash.png") "Runtime splash"
    Assert-File (Join-Path $Directory "branding\vespera_logo_sting.wav") "Runtime startup sting"

    $ProjectText = Get-Content -LiteralPath $ProjectFile -Raw
    if ($ProjectText -match '(?m)^managed_assembly\s+"(?<assembly>[^"]+)"') {
        $Assembly = $Matches.assembly
        Assert-File (Join-Path $Directory "managed\Vespera.Managed.runtimeconfig.json") "Managed runtime config"
        Assert-File (Join-Path $Directory "managed\Vespera.NET.dll") "Vespera.NET bridge"
        Assert-File (Join-Path $Directory ("managed\{0}.dll" -f $Assembly)) "Game managed assembly"
    }

    if ($RequirePortable) {
        $HostFxr = @(Get-ChildItem -LiteralPath (Join-Path $Directory "dotnet\host\fxr") -Filter "hostfxr.dll" -Recurse -File -ErrorAction SilentlyContinue)
        if ($HostFxr.Count -lt 1) { throw "Portable package is missing dotnet\host\fxr\*\hostfxr.dll." }
        $CoreRuntime = @(Get-ChildItem -LiteralPath (Join-Path $Directory "dotnet\shared\Microsoft.NETCore.App") -Directory -ErrorAction SilentlyContinue)
        if ($CoreRuntime.Count -lt 1) { throw "Portable package is missing Microsoft.NETCore.App." }
    }
    return $Runtime
}

function Test-RuntimeLaunch([string]$Runtime, [string]$WorkingDirectory) {
    $Process = Start-Process -FilePath $Runtime -WorkingDirectory $WorkingDirectory -PassThru
    try {
        Start-Sleep -Seconds ([Math]::Max($LaunchSeconds, 1))
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Packaged runtime exited during startup smoke test with code $($Process.ExitCode): $Runtime"
        }
    } finally {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(3000) | Out-Null
        }
    }
}

try {
    Set-Location $Root

    Invoke-Step "Source/package preflight" { Test-SourcePackagePreflight }

    if (-not $SkipTests) {
        Invoke-Step "Behavioral tests" { & (Join-Path $Root "test.ps1") }
    }

    if (-not $NoBuild) {
        Invoke-Step "Release build" { & (Join-Path $Root "build.ps1") -Configuration Release }
    }

    $Hub = Join-Path $Root "build\examples\project_hub\Release\vespera_project_hub.exe"
    Assert-File $Hub "Release Project Hub"

    $HubResult = Join-Path $Work "hub-result.txt"
    Invoke-Step "Fresh Project Hub starter creation" {
        & $Hub `
            "--templates=$(Join-Path $Root 'templates')" `
            "--result=$HubResult" `
            "--create-template=3d" `
            "--project-name=Vespera Release Gate" `
            "--project-location=$Projects"
        if ($LASTEXITCODE -ne 0) { throw "Project Hub starter creation failed with exit code $LASTEXITCODE." }
    }
    Assert-File $HubResult "Project Hub result"
    $StarterProject = (Get-Content -LiteralPath $HubResult | Select-Object -First 1).Trim()
    Assert-File $StarterProject "Hub-created starter project"

    # Exercise the explicit 2D product decision without pretending it is a
    # complete dedicated 2D engine mode: creation remains supported as an
    # experimental 2D/UI foundation.
    $TwoDResult = Join-Path $Work "hub-result-2d.txt"
    Invoke-Step "Experimental 2D/UI foundation creation" {
        & $Hub `
            "--templates=$(Join-Path $Root 'templates')" `
            "--result=$TwoDResult" `
            "--create-template=2d" `
            "--project-name=Vespera 2D Foundation Gate" `
            "--project-location=$Projects"
        if ($LASTEXITCODE -ne 0) { throw "2D/UI foundation creation failed with exit code $LASTEXITCODE." }
    }
    Assert-File $TwoDResult "2D/UI Hub result"
    $TwoDProject = (Get-Content -LiteralPath $TwoDResult | Select-Object -First 1).Trim()
    Assert-File $TwoDProject "Hub-created 2D/UI foundation project"
    $TwoDProjectText = Get-Content -LiteralPath $TwoDProject -Raw
    if ($TwoDProjectText -notmatch 'name\s+"Vespera 2D Foundation Gate"') {
        throw "2D/UI foundation project did not preserve the requested project name: $TwoDProject"
    }

    $StarterOutput = Join-Path $Exports "starter-release-portable"
    Invoke-Step "Hub-created starter Release portable export" {
        & (Join-Path $Root "export.ps1") `
            -Project $StarterProject `
            -Configuration Release `
            -ManagedDeployment Portable `
            -Output $StarterOutput `
            -NoBuild
    }
    $StarterRuntime = Test-Package $StarterOutput $true
    $RuntimeLog = $null
    if ($env:LOCALAPPDATA) {
        $LogName = [System.IO.Path]::GetFileNameWithoutExtension($StarterRuntime) + ".log"
        $RuntimeLog = Join-Path $env:LOCALAPPDATA "Vespera\Logs\$LogName"
        Remove-Item -LiteralPath $RuntimeLog -Force -ErrorAction SilentlyContinue
    }
    Invoke-Step "Hub-created starter standalone launch" { Test-RuntimeLaunch $StarterRuntime $StarterOutput }
    if ($RuntimeLog) {
        Assert-File $RuntimeLog "Release runtime log"
        $RuntimeLogText = Get-Content -LiteralPath $RuntimeLog -Raw
        if ($RuntimeLogText -notmatch 'Vespera runtime log opened' -or $RuntimeLogText -notmatch 'Vespera Player') {
            throw "Release runtime log was created but does not contain the expected startup diagnostics: $RuntimeLog"
        }
    }

    $OrdinaryProjects = @(
        @{ Name = "Emberlight Guild"; Project = Join-Path $Root "examples\emberlight_guild\EmberlightGuild.vesperaproject"; Output = Join-Path $Exports "emberlight-release" },
        @{ Name = "Performance Lab"; Project = Join-Path $Root "examples\performance_lab\VesperaPerformanceLab.vesperaproject"; Output = Join-Path $Exports "performance-lab-release" }
    )
    foreach ($Item in $OrdinaryProjects) {
        Invoke-Step "$($Item.Name) ordinary-project Release export" {
            & (Join-Path $Root "export.ps1") `
                -Project $Item.Project `
                -Configuration Release `
                -ManagedDeployment FrameworkDependent `
                -Output $Item.Output `
                -NoBuild
        }
        $Runtime = Test-Package $Item.Output $false
        Invoke-Step "$($Item.Name) standalone startup" { Test-RuntimeLaunch $Runtime $Item.Output }
    }

    Write-Host "`nVespera 1.0.0 release gate PASSED." -ForegroundColor Green
    Write-Host "Fresh Hub project -> Release portable package -> standalone launch passed."
    Write-Host "Emberlight Guild and Performance Lab also exported/launched through the ordinary shared-player path."
    Write-Host "Work directory: $Work" -ForegroundColor DarkGray
} finally {
    Set-Location $Root
    if (-not $KeepWorkDirectory) {
        Remove-Item -LiteralPath $Work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
