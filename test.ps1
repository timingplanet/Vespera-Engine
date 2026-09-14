param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build-tests"

function Find-CMake {
    $OnPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($OnPath) { return $OnPath.Source }

    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $VsWhere) {
        $Installations = & $VsWhere -products * -all -property installationPath
        foreach ($Installation in $Installations) {
            if (-not $Installation) { continue }
            $Candidate = Join-Path $Installation "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $Candidate) { return $Candidate }
        }
    }

    foreach ($Candidate in @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
    )) {
        if ($Candidate -and (Test-Path $Candidate)) { return $Candidate }
    }
    return $null
}

$CMake = Find-CMake
if (-not $CMake) {
    throw "CMake could not be located. Install Visual Studio's 'C++ CMake tools for Windows' component or a current standalone CMake release."
}

$CMakeHelp = (& $CMake --help | Out-String)
$Generator = if ($CMakeHelp -match [regex]::Escape("Visual Studio 18 2026")) {
    "Visual Studio 18 2026"
} elseif ($CMakeHelp -match [regex]::Escape("Visual Studio 17 2022")) {
    "Visual Studio 17 2022"
} else {
    $null
}
if (-not $Generator) {
    throw "No supported Visual Studio CMake generator was found. Vespera currently supports Visual Studio 2026 or 2022."
}

$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CacheFile) {
    $CachedGeneratorLine = Select-String -Path $CacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' | Select-Object -First 1
    if ($CachedGeneratorLine) {
        $CachedGenerator = $CachedGeneratorLine.Matches[0].Groups[1].Value
        if ($CachedGenerator -ne $Generator) {
            Remove-Item -Recurse -Force $BuildDir
        }
    }
}

Write-Host "Vespera behavioral tests" -ForegroundColor Cyan
Write-Host "Generator: $Generator" -ForegroundColor DarkCyan

& $CMake -S $Root -B $BuildDir -G $Generator -A x64 -DVESPERA_TESTS_ONLY=ON
if ($LASTEXITCODE -ne 0) { throw "Vespera test configuration failed with exit code $LASTEXITCODE." }

& $CMake --build $BuildDir --config $Configuration --target vespera_engine_logic_tests
if ($LASTEXITCODE -ne 0) { throw "Vespera test build failed with exit code $LASTEXITCODE." }

$CTest = Join-Path (Split-Path -Parent $CMake) "ctest.exe"
if (-not (Test-Path $CTest)) {
    $CTestCommand = Get-Command ctest -ErrorAction SilentlyContinue
    if ($CTestCommand) { $CTest = $CTestCommand.Source }
}
if (-not (Test-Path $CTest)) { throw "ctest could not be located next to CMake or on PATH." }

& $CTest --test-dir $BuildDir -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) {
    $TestExitCode = $LASTEXITCODE
    $LastTestLog = Join-Path $BuildDir "Testing\Temporary\LastTest.log"
    if (Test-Path $LastTestLog) {
        Write-Host "`nCTest LastTest.log (tail):" -ForegroundColor Yellow
        Get-Content -LiteralPath $LastTestLog -Tail 160
    }
    throw "Vespera behavioral tests failed with exit code $TestExitCode."
}

Write-Host "`nVespera behavioral tests passed." -ForegroundColor Green
