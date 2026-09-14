param(
    [switch]$NoBuild,
    [switch]$SkipReleaseGate,
    [switch]$KeepEditor,
    [int]$AutomationPort = 46787,
    [ValidateRange(2,64)]
    [int]$PlayCycles = 24,
    [ValidateRange(1,64)]
    [int]$SceneSwitches = 16
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ReferenceSourceDirectory = Join-Path $Root "examples\reference_game"
$QaReferenceDirectory = Join-Path $Root "examples\__vespera_rc_qa_reference"
$ReferenceProject = Join-Path $QaReferenceDirectory "VesperaReference.vesperaproject"
$ReferenceManagedProject = Join-Path $QaReferenceDirectory "managed\ReferenceGame.Scripts.csproj"
$Editor = Join-Path $Root "build\editor\Release\vespera_editor.exe"
$EditorDirectory = Split-Path -Parent $Editor
$ManagedHelper = Join-Path $Root "tools\build-managed-editor.ps1"
$ManagedStage = Join-Path $Root "build\managed\rc-reference"
$ManagedDiagnostics = Join-Path $ManagedStage "Vespera.ManagedBuildDiagnostics.txt"
$EditorManaged = Join-Path $EditorDirectory "managed"
$EditorProcess = $null

function Invoke-Step([string]$Title, [scriptblock]$Body) {
    Write-Host "`n== $Title ==" -ForegroundColor Cyan
    & $Body
}

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
}

function Find-Python {
    $Python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $Python) { $Python = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $Python) { throw "Python 3 is required to run the Vespera RC gate." }
    return $Python.Source
}

function Test-TcpPort([string]$HostName, [int]$Port, [int]$TimeoutMilliseconds = 250) {
    $Client = [System.Net.Sockets.TcpClient]::new()
    try {
        $Async = $Client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $Async.AsyncWaitHandle.WaitOne($TimeoutMilliseconds)) { return $false }
        $Client.EndConnect($Async)
        return $true
    } catch {
        return $false
    } finally {
        $Client.Dispose()
    }
}

function Wait-AutomationPort([System.Diagnostics.Process]$Process, [int]$Port, [int]$TimeoutSeconds = 45) {
    $Deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $Deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Vespera Editor exited before automation became available (exit code $($Process.ExitCode))."
        }
        if (Test-TcpPort "127.0.0.1" $Port) { return }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for Vespera Editor automation on 127.0.0.1:$Port."
}

try {
    Set-Location $Root
    $Python = Find-Python

    Invoke-Step "RC source validation" {
        & $Python (Join-Path $Root "tools\validate_source.py")
        if ($LASTEXITCODE -ne 0) { throw "Source validation failed with exit code $LASTEXITCODE." }
    }

    Invoke-Step "Public release hygiene" {
        & $Python (Join-Path $Root "tools\validate_public_release.py")
        if ($LASTEXITCODE -ne 0) { throw "Public release validation failed with exit code $LASTEXITCODE." }
    }

    if (-not $SkipReleaseGate) {
        Invoke-Step "Validated release/public workflow" {
            if ($NoBuild) {
                & (Join-Path $Root "tools\run-release-gate.ps1") -NoBuild
            } else {
                & (Join-Path $Root "tools\run-release-gate.ps1")
            }
        }
    }

    Invoke-Step "Prepare disposable reference QA project" {
        if (Test-Path -LiteralPath $QaReferenceDirectory) {
            Remove-Item -LiteralPath $QaReferenceDirectory -Recurse -Force
        }
        Copy-Item -LiteralPath $ReferenceSourceDirectory -Destination $QaReferenceDirectory -Recurse -Force
        Assert-File $ReferenceProject "Disposable reference QA project"
        Assert-File $ReferenceManagedProject "Disposable reference managed project"
    }

    Assert-File $Editor "Release Vespera Editor"
    Assert-File $ReferenceProject "Reference QA project"
    Assert-File $ReferenceManagedProject "Reference managed project"
    Assert-File $ManagedHelper "Managed build helper"

    Invoke-Step "Reference C# RC stage" {
        if (Test-Path -LiteralPath $ManagedStage) { Remove-Item -LiteralPath $ManagedStage -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $ManagedStage | Out-Null
        & $ManagedHelper `
            -Project $ReferenceManagedProject `
            -GameAssemblyName "ReferenceGame.Scripts" `
            -OutputDir $ManagedStage `
            -DiagnosticsFile $ManagedDiagnostics `
            -NoMirrors
        if ($LASTEXITCODE -ne 0) { throw "Reference managed build failed with exit code $LASTEXITCODE." }
        Assert-File (Join-Path $ManagedStage "ReferenceGame.Scripts.dll") "Reference managed assembly"
        Assert-File (Join-Path $ManagedStage "Vespera.NET.dll") "Vespera.NET bridge"
        if (Test-Path -LiteralPath $EditorManaged) { Remove-Item -LiteralPath $EditorManaged -Recurse -Force }
        Copy-Item -LiteralPath $ManagedStage -Destination $EditorManaged -Recurse -Force
    }

    # The editor's managed-recovery/build QA uses these same public source-checkout hooks.
    $env:VESPERA_MANAGED_BUILD_HELPER = $ManagedHelper
    $env:VESPERA_SOURCE_ROOT = $Root

    Invoke-Step "Start Release editor automation" {
        $EditorArgs = @("`"$ReferenceProject`"", "--automation-port=$AutomationPort")
        $script:EditorProcess = Start-Process -FilePath $Editor -ArgumentList $EditorArgs -WorkingDirectory $EditorDirectory -PassThru
        Wait-AutomationPort $script:EditorProcess $AutomationPort
        Write-Host "Release editor automation ready on 127.0.0.1:$AutomationPort." -ForegroundColor DarkGray
    }

    Invoke-Step "RC adversarial editor/runtime QA" {
        $QaArgs = @(
            (Join-Path $Root "tools\vespera_qa_runner.py"),
            "--port", "$AutomationPort",
            "--managed-recovery",
            "--mcp-managed-build",
            "--asset-move-save",
            "--runtime-telemetry",
            "--runtime-export-automation",
            "--runtime-export-configuration", "Release",
            "--stress",
            "--play-cycles", "$PlayCycles",
            "--scene-switches", "$SceneSwitches"
        )
        & $Python @QaArgs
        if ($LASTEXITCODE -ne 0) { throw "RC adversarial QA failed with exit code $LASTEXITCODE." }
    }

    Invoke-Step "RC post-QA source validation" {
        & $Python (Join-Path $Root "tools\validate_source.py")
        if ($LASTEXITCODE -ne 0) { throw "Post-QA source validation failed with exit code $LASTEXITCODE." }
    }

    # Public-release hygiene is intentionally a preflight check. The RC gate itself
    # creates build/, build-tests/, managed obj/bin output, runtime logs, and a
    # disposable QA project, so rerunning the distributable-tree hygiene scanner
    # after QA would reject the expected products of this gate. Post-QA integrity
    # is covered by validate_source.py above; the disposable QA project is removed
    # in the finally block below.

    Write-Host "`nVespera 1.0.0 RC gate PASSED." -ForegroundColor Green
    Write-Host "One Release build covered the public release path; the same build then passed managed recovery, repeated Play/Stop, scene switching, asset move/save/reopen, runtime telemetry, Release exported-runtime automation, MCP managed build and scale stress."
} finally {
    Set-Location $Root
    if (Test-Path -LiteralPath $QaReferenceDirectory) {
        Remove-Item -LiteralPath $QaReferenceDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($EditorProcess -and -not $KeepEditor) {
        try {
            $EditorProcess.Refresh()
            if (-not $EditorProcess.HasExited) {
                Stop-Process -Id $EditorProcess.Id -Force -ErrorAction SilentlyContinue
                $EditorProcess.WaitForExit(3000) | Out-Null
            }
        } catch {}
    }
}
