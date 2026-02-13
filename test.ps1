# Power Monitor Test Runner
# This script builds and runs the Google Test suite

# Parse command line arguments - must be at the beginning
param(
    [switch]$Clean,
    [switch]$Rebuild,
    [switch]$Verbose,
    [switch]$Help
)

# Exit on error
$ErrorActionPreference = "Stop"

# Colors for output
function Print-Info {
    param([string]$Message)
    Write-Host "[INFO] " -ForegroundColor Blue -NoNewline
    Write-Host $Message
}

function Print-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Print-Error-Custom {
    param([string]$Message)
    Write-Host "[ERROR] " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Print-Warning {
    param([string]$Message)
    Write-Host "[WARNING] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

# Script directory
$ScriptDir = $PSScriptRoot
if ([string]::IsNullOrEmpty($ScriptDir)) {
    $ScriptDir = Split-Path -Parent -Path $MyInvocation.MyCommand.Definition
}
$BuildDir = Join-Path $ScriptDir "build"

Print-Info "Power Monitor Test Suite"
Write-Host "======================================"

if ($Help) {
    Write-Host "Usage: .\test.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Clean      Clean build directory before building"
    Write-Host "  -Rebuild    Rebuild all targets (equivalent to -Clean)"
    Write-Host "  -Verbose    Verbose output from tests"
    Write-Host "  -Help       Show this help message"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\test.ps1                # Run tests with existing build"
    Write-Host "  .\test.ps1 -Clean         # Clean build and run tests"
    Write-Host "  .\test.ps1 -Verbose       # Run tests with verbose output"
    exit 0
}

# Clean build if requested
if ($Clean -or $Rebuild) {
    Print-Info "Cleaning build directory..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
}

# Configure CMake - detect generator
$Generator = "Visual Studio 18 2026"
$Arch = "x64"
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
    $Arch = "ARM64"
}

$CMakeCachePath = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $CMakeCachePath)) {
    Print-Info "Configuring CMake with generator: $Generator -A $Arch"
    & cmake -B $BuildDir -S $ScriptDir -G $Generator -A $Arch
    if ($LASTEXITCODE -ne 0) {
        Print-Error-Custom "CMake configuration failed"
        exit 1
    }
    Print-Success "CMake configured"
} else {
    Print-Info "Using existing CMake configuration"
}

# Build
Print-Info "Building project..."
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Print-Error-Custom "Build failed"
    exit 1
}
Print-Success "Build completed"

# Check if test executable exists
$TestExec = Join-Path $BuildDir "pc_sim"
$TestExec = Join-Path $TestExec "Debug"
$TestExec = Join-Path $TestExec "pc_sim_test.exe"
if (-not (Test-Path $TestExec)) {
    # Try Release configuration
    $TestExec = Join-Path $BuildDir "pc_sim"
    $TestExec = Join-Path $TestExec "Release"
    $TestExec = Join-Path $TestExec "pc_sim_test.exe"
    if (-not (Test-Path $TestExec)) {
        # Try without .exe extension for non-Windows builds
        $TestExec = Join-Path $BuildDir "pc_sim/pc_sim_test"
        if (-not (Test-Path $TestExec)) {
            Print-Error-Custom "Test executable not found"
            exit 1
        }
    }
}

Write-Host ""
Print-Info "Running tests..."
Write-Host "======================================"

# Run tests
if ($Verbose) {
    & $TestExec --gtest_color=yes
} else {
    & $TestExec --gtest_color=yes --gtest_brief=1
}

$TestResult = $LASTEXITCODE

Write-Host "======================================"

if ($TestResult -eq 0) {
    Print-Success "All tests passed!"
    exit 0
} else {
    Print-Error-Custom "Some tests failed!"
    Write-Host ""
    Print-Info "Tip: Run with -Verbose flag for detailed output"
    exit 1
}
