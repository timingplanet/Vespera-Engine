param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $Root "run-hub.ps1") -Configuration $Configuration
