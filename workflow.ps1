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
    Write-Host "Usage: .\workflow.ps1 [OPTIONS]"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -Clean      Clean build directory before building"
    Write-Host "  -Rebuild    Rebuild all targets (equivalent to -Clean)"
    Write-Host "  -Verbose    Verbose output from tests"
    Write-Host "  -Help       Show this help message"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\workflow.ps1                # Run tests with existing build"
    Write-Host "  .\workflow.ps1 -Clean         # Clean build and run tests"
    Write-Host "  .\workflow.ps1 -Verbose       # Run tests with verbose output"
    exit 0
}

# OS Detection
$CurrentIsLinux = $false
$CurrentIsWindows = $false

if ($IsLinux) {
    $CurrentIsLinux = $true
} elseif ($IsWindows) {
    $CurrentIsWindows = $true
} else {
    # Fallback for older PowerShell versions or other platforms
    if ($PSVersionTable.Platform -eq "Unix") {
        $CurrentIsLinux = $true
    } else {
        # Default to Windows behavior if not Unix
        $CurrentIsWindows = $true
    }
}

if ($CurrentIsLinux) {
    Print-Info "Detected OS: Linux"
} else {
    Print-Info "Detected OS: Windows"
}

# Clean build if requested
if ($Clean -or $Rebuild) {
    Print-Info "Cleaning build directory..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
}

# Configure CMake
$CMakeCachePath = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $CMakeCachePath)) {
    Print-Info "Configuring CMake..."

    $CMakeArgs = @("-B", $BuildDir, "-S", $ScriptDir)

    if ($CurrentIsWindows) {
        # Windows configuration
        $Generator = "Visual Studio 18 2026"

        # Check if running in GitHub Actions and use a supported generator
        if ($env:GITHUB_ACTIONS -eq 'true') {
            $Generator = "Visual Studio 17 2022"
            Print-Info "Running in GitHub Actions: Overriding generator to $Generator"
        }

        $Arch = "x64"
        if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
            $Arch = "ARM64"
        }

        Print-Info "Using Generator: $Generator -A $Arch"
        $CMakeArgs += "-G", $Generator, "-A", $Arch
    } else {
        # Linux configuration - let CMake pick default
        Print-Info "Using default generator"
    }

    & cmake $CMakeArgs
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
$TestExec = $null

if ($CurrentIsWindows) {
    # Windows: Look in Debug/Release subdirectories with .exe extension
    $TestExecPathDebug = Join-Path $BuildDir "pc_sim"
    $TestExecPathDebug = Join-Path $TestExecPathDebug "Debug"
    $TestExecPathDebug = Join-Path $TestExecPathDebug "pc_sim_test.exe"

    if (Test-Path $TestExecPathDebug) {
        $TestExec = $TestExecPathDebug
    } else {
        $TestExecPathRelease = Join-Path $BuildDir "pc_sim"
        $TestExecPathRelease = Join-Path $TestExecPathRelease "Release"
        $TestExecPathRelease = Join-Path $TestExecPathRelease "pc_sim_test.exe"

        if (Test-Path $TestExecPathRelease) {
            $TestExec = $TestExecPathRelease
        }
    }
} else {
    # Linux: Look directly in target directory without extension
    # Try typical Makefile output location
    $TestExecPath = Join-Path $BuildDir "pc_sim"
    $TestExecPath = Join-Path $TestExecPath "pc_sim_test"

    if (Test-Path $TestExecPath) {
        $TestExec = $TestExecPath
    }
}

if (-not $TestExec) {
    Print-Error-Custom "Test executable not found"
    if ($CurrentIsWindows) {
        Print-Info "Checked Debug/Release subdirectories for pc_sim_test.exe"
    } else {
        Print-Info "Checked for pc_sim_test in build directory"
    }
    exit 1
}

Write-Host ""
Print-Info "Running tests..."
Write-Host "Using executable: $TestExec"
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
