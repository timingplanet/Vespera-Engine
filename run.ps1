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
$Exe = Join-Path $Root "build\editor\$Configuration\vespera_editor.exe"
if (-not $Project) { $Project = Join-Path $Root "examples\reference_game\VesperaReference.vesperaproject" }
elseif (-not [System.IO.Path]::IsPathRooted($Project)) { $Project = Join-Path (Get-Location) $Project }
$Project = [System.IO.Path]::GetFullPath($Project)

if (-not (Test-Path $Exe)) {
    Write-Host "Vespera Editor is not built yet. Running build.ps1 first..." -ForegroundColor Yellow
    & (Join-Path $Root "build.ps1") -Configuration $Configuration
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

function Read-VesperaProjectSetting([string]$Path, [string]$Key) {
    if (-not (Test-Path $Path)) { return $null }
    $Pattern = '^' + [regex]::Escape($Key) + '\s+"(?<value>.*)"\s*$'
    foreach ($Line in Get-Content $Path) { if ($Line -match $Pattern) { return $Matches.value } }
    return $null
}

# Source-tree convenience only: compile the selected project's managed scripts
# through the same native builder used by an installed Vespera Editor. The
# output belongs to the project, not the editor executable directory.
$ManagedProjectSetting = Read-VesperaProjectSetting $Project "managed_project"
$ManagedAssembly = Read-VesperaProjectSetting $Project "managed_assembly"
if ($ManagedProjectSetting -and $ManagedAssembly) {
    $Builder = Join-Path $Root "build\tools\builder\$Configuration\vespera_builder.exe"
    $Dotnet = Find-Dotnet
    if ((Test-Path $Builder) -and $Dotnet) {
        $ProjectDir = Split-Path -Parent $Project
        $ManagedConfiguration = if ($Configuration -eq "Debug") { "Debug" } else { "Release" }
        $ManagedOutput = Join-Path $ProjectDir ".vespera\managed"
        Write-Host "Building project C# scripts before editor launch..." -ForegroundColor Cyan
        & $Builder `
            --project $Project `
            --managed-only `
            --managed-output $ManagedOutput `
            --configuration $ManagedConfiguration `
            --dotnet $Dotnet `
            --sdk-project (Join-Path $Root "managed\Vespera.NET\Vespera.NET.csproj") `
            --script-tool-project (Join-Path $Root "managed\Vespera.ScriptTool\Vespera.ScriptTool.csproj")
        if ($LASTEXITCODE -ne 0) { throw "Project C# build failed with exit code $LASTEXITCODE." }
    } elseif (-not $Dotnet) {
        Write-Warning "dotnet SDK not found. Editor will open, but project C# scripts cannot be rebuilt until .NET 8+ is available."
    }
}

$ExeDir = Split-Path -Parent $Exe
$ExeName = Split-Path -Leaf $Exe
Push-Location $ExeDir
try {
    $EditorArgs = @($Project)
    if ($Renderer -ne "auto") { $EditorArgs += "--renderer=$Renderer" }
    if ($Automation) { $EditorArgs += "--automation-port=$AutomationPort" }
    & (Join-Path $ExeDir $ExeName) @EditorArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Vespera Editor exited with code $LASTEXITCODE." -ForegroundColor Red
        $SettingsRoot = if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA "Vespera" } else { $ExeDir }
        $Trace = Join-Path $SettingsRoot "vespera_editor_startup.log"
        if (Test-Path $Trace) {
            Write-Host "Startup trace: $Trace" -ForegroundColor Yellow
            Get-Content $Trace
        }
    }
} finally {
    Pop-Location
}
