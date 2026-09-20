param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug",
    [switch]$Automation,
    [int]$AutomationPort = 46787
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Hub = Join-Path $Root "build\examples\project_hub\$Configuration\vespera_project_hub.exe"

if (-not (Test-Path $Hub)) {
    Write-Host "Vespera Project Hub is not built yet. Running build.ps1 first..." -ForegroundColor Yellow
    & (Join-Path $Root "build.ps1") -Configuration $Configuration
}
if (-not (Test-Path $Hub)) { throw "Project Hub executable was not produced: $Hub" }

$HubDir = Split-Path -Parent $Hub
$Result = Join-Path $HubDir "vespera_hub_result.txt"
if (Test-Path $Result) { Remove-Item $Result -Force }

Write-Host "Opening Vespera Project Hub..." -ForegroundColor Cyan
Push-Location $HubDir
try {
    & $Hub "--templates=$(Join-Path $HubDir 'templates')" "--result=$Result" "--no-launch-editor"
} finally {
    Pop-Location
}

if (Test-Path $Result) {
    $Selected = (Get-Content $Result | Select-Object -First 1).Trim()
    Remove-Item $Result -Force
    if ($Selected) {
        Write-Host "Opening project: $Selected" -ForegroundColor Cyan
        & (Join-Path $Root "run-editor.ps1") -Project $Selected -Configuration $Configuration -Automation:$Automation -AutomationPort $AutomationPort
    }
}
