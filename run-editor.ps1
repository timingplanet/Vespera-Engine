param(
    [switch]$Automation,
    [int]$AutomationPort = 46787,
    [string]$Project = "",
    [ValidateSet("auto", "d3d12", "vulkan")]
    [string]$Renderer = "auto",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $Root "run.ps1") -Automation:$Automation -AutomationPort $AutomationPort -Project $Project -Renderer $Renderer -Configuration $Configuration
