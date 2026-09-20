param(
    [switch]$SkipAutomated,
    [switch]$SkipManual,
    [switch]$NoBuild,
    [ValidateRange(2,64)]
    [int]$PlayCycles = 24,
    [ValidateRange(1,64)]
    [int]$SceneSwitches = 16,
    [int]$AutomationPort = 46787
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Failures = [System.Collections.Generic.List[string]]::new()
$Skips = [System.Collections.Generic.List[string]]::new()
$Passes = [System.Collections.Generic.List[string]]::new()
$LogDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "Vespera-RC-Logs"
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$LogFile = Join-Path $LogDirectory ("Vespera-RC-{0}-{1}.log" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $PID)
$TranscriptStarted = $false
try {
    Start-Transcript -LiteralPath $LogFile -Force | Out-Null
    $TranscriptStarted = $true
} catch {
    Write-Host "[WARN] Could not start PowerShell transcript: $($_.Exception.Message)" -ForegroundColor Yellow
}

function Write-Result([string]$Status, [string]$Name, [string]$Detail = "") {
    $Prefix = "[$Status]"
    if ($Status -eq "PASS") { Write-Host "$Prefix $Name" -ForegroundColor Green }
    elseif ($Status -eq "FAIL") { Write-Host "$Prefix $Name" -ForegroundColor Red }
    elseif ($Status -eq "SKIP") { Write-Host "$Prefix $Name" -ForegroundColor Yellow }
    else { Write-Host "$Prefix $Name" }
    if ($Detail) { Write-Host "       $Detail" -ForegroundColor DarkGray }
}

function Record-Pass([string]$Name) {
    $Passes.Add($Name)
    Write-Result "PASS" $Name
}

function Record-Fail([string]$Name, [string]$Detail) {
    $Failures.Add($Name)
    Write-Result "FAIL" $Name $Detail
}

function Record-Skip([string]$Name, [string]$Detail = "") {
    $Skips.Add($Name)
    Write-Result "SKIP" $Name $Detail
}

function Invoke-Automated([string]$Name, [scriptblock]$Body) {
    try {
        # Consume success-stream output at the host so the caller receives only
        # the scalar Boolean result. Without this, command output plus $false
        # becomes a non-empty array that PowerShell treats as true.
        & $Body | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Command exited with code $LASTEXITCODE." }
        Record-Pass $Name
        return $true
    } catch {
        Record-Fail $Name $_.Exception.Message
        return $false
    }
}

function Invoke-Manual([string]$Name, [scriptblock]$Launch) {
    Write-Host "`n--- MANUAL: $Name ---" -ForegroundColor Cyan
    Write-Host "Close the launched window when you are done looking at it." -ForegroundColor DarkGray
    try {
        & $Launch
        if ($LASTEXITCODE -ne 0) { throw "Launch command exited with code $LASTEXITCODE." }
    } catch {
        Record-Fail $Name $_.Exception.Message
        return
    }

    while ($true) {
        $Answer = (Read-Host "Did it look/run correctly? [p]ass / [f]ail / [s]kip").Trim().ToLowerInvariant()
        if ($Answer -in @("p", "pass", "y", "yes")) { Record-Pass $Name; return }
        if ($Answer -in @("f", "fail", "n", "no")) { Record-Fail $Name "Marked failed by tester."; return }
        if ($Answer -in @("s", "skip")) { Record-Skip $Name "Skipped by tester."; return }
        Write-Host "Enter p, f, or s." -ForegroundColor Yellow
    }
}

Set-Location $Root
Write-Host "Vespera RC validation" -ForegroundColor Cyan
Write-Host "This runs the automated RC gate once, then four visual smoke tests." -ForegroundColor DarkGray
Write-Host "Automated QA may open Vespera windows. Do not close or interact with them unless the script asks you to." -ForegroundColor Yellow
Write-Host "Full test transcript: $LogFile" -ForegroundColor DarkGray

$AutomatedPassed = $true
if ($SkipAutomated) {
    Record-Skip "Automated RC gate" "-SkipAutomated was supplied."
} else {
    $Gate = Join-Path $Root "tools\run-rc-gate.ps1"
    $AutomatedPassed = Invoke-Automated "Automated RC gate" {
        if ($NoBuild) {
            & $Gate -AutomationPort $AutomationPort -PlayCycles $PlayCycles -SceneSwitches $SceneSwitches -NoBuild
        } else {
            & $Gate -AutomationPort $AutomationPort -PlayCycles $PlayCycles -SceneSwitches $SceneSwitches
        }
    }
}

if ($SkipManual) {
    Record-Skip "D3D12 editor visual smoke" "-SkipManual was supplied."
    Record-Skip "D3D12 reference-game visual smoke" "-SkipManual was supplied."
    Record-Skip "Vulkan editor visual smoke" "-SkipManual was supplied."
    Record-Skip "Vulkan reference-game visual smoke" "-SkipManual was supplied."
} elseif (-not $AutomatedPassed -and -not $SkipAutomated) {
    Record-Skip "D3D12 editor visual smoke" "Automated RC gate failed first."
    Record-Skip "D3D12 reference-game visual smoke" "Automated RC gate failed first."
    Record-Skip "Vulkan editor visual smoke" "Automated RC gate failed first."
    Record-Skip "Vulkan reference-game visual smoke" "Automated RC gate failed first."
} else {
    Invoke-Manual "D3D12 editor visual smoke" {
        & (Join-Path $Root "run.ps1") -Configuration Release -Renderer d3d12
    }
    Invoke-Manual "D3D12 reference-game visual smoke" {
        & (Join-Path $Root "run-game.ps1") -Configuration Release -Renderer d3d12
    }
    Invoke-Manual "Vulkan editor visual smoke" {
        & (Join-Path $Root "run.ps1") -Configuration Release -Renderer vulkan
    }
    Invoke-Manual "Vulkan reference-game visual smoke" {
        & (Join-Path $Root "run-game.ps1") -Configuration Release -Renderer vulkan
    }
}

Write-Host "`n=============================="
Write-Host "RC TEST SUMMARY"
Write-Host "PASS: $($Passes.Count)"
Write-Host "FAIL: $($Failures.Count)"
Write-Host "SKIP: $($Skips.Count)"

$FinalExitCode = 0
if ($Failures.Count -gt 0) {
    Write-Host "RESULT: FAIL" -ForegroundColor Red
    foreach ($Failure in $Failures) { Write-Host "  - $Failure" -ForegroundColor Red }
    Write-Host "Full failure transcript: $LogFile" -ForegroundColor Yellow
    $FinalExitCode = 1
} else {
    Write-Host "RESULT: PASS" -ForegroundColor Green
    if ($Skips.Count -gt 0) {
        Write-Host "Some checks were skipped; see the lines above." -ForegroundColor Yellow
    }
    Write-Host "Full test transcript: $LogFile" -ForegroundColor DarkGray
}

if ($TranscriptStarted) {
    try { Stop-Transcript | Out-Null } catch {}
}
exit $FinalExitCode
