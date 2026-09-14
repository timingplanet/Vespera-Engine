param(
    [int]$Port = 46787,
    [switch]$Export,
    [switch]$ManagedRecovery,
    [switch]$McpManagedBuild,
    [switch]$AssetMoveSave,
    [switch]$RuntimeTelemetry,
    [switch]$RuntimeExportAutomation,
    [switch]$Stress,
    [ValidateRange(2,64)]
    [int]$PlayCycles = 4,
    [ValidateRange(0,64)]
    [int]$SceneSwitches = 0,
    [ValidateSet("Debug", "Development", "Release")]
    [string]$RuntimeExportConfiguration = "Debug"
)
$ErrorActionPreference = "Stop"
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $Python) { throw "Python 3 is required to run Vespera QA." }
$Args = @("$PSScriptRoot/vespera_qa_runner.py", "--port", "$Port")
if ($Export) { $Args += "--export" }
if ($ManagedRecovery) { $Args += "--managed-recovery" }
if ($McpManagedBuild) { $Args += "--mcp-managed-build" }
if ($AssetMoveSave) { $Args += "--asset-move-save" }
if ($RuntimeTelemetry) { $Args += "--runtime-telemetry" }
if ($RuntimeExportAutomation) { $Args += "--runtime-export-automation" }
if ($Stress) { $Args += "--stress" }
$Args += @("--play-cycles", "$PlayCycles", "--scene-switches", "$SceneSwitches", "--runtime-export-configuration", $RuntimeExportConfiguration)
& $Python.Source @Args
exit $LASTEXITCODE
