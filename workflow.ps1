# Power Monitor Workflow Script
# This script builds and runs the Google Test suite, and can also build device firmware

# Parse command line arguments - must be at the beginning
param(
    [switch]$Clean,
    [switch]$Rebuild,
    [switch]$Verbose,
    [switch]$Help,
    [switch]$GenerateSolution,
    [switch]$OpenVS,
    [switch]$BuildDevice,
    [switch]$FlashPico
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
    Write-Host "  -Clean           Clean build directory before building"
    Write-Host "  -Rebuild         Rebuild all targets (equivalent to -Clean)"
    Write-Host "  -Verbose         Verbose output from tests"
    Write-Host "  -GenerateSolution Generate Visual Studio solution files only"
    Write-Host "  -OpenVS          Generate solution and open in Visual Studio"
    Write-Host "  -BuildDevice     Build device firmware (requires PICO_SDK_PATH)"
    Write-Host "  -FlashPico      Flash device firmware to Pico (requires PICO_SDK_PATH, Windows only)"
    Write-Host "  -Help            Show this help message"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\workflow.ps1                # Run tests with existing build"
    Write-Host "  .\workflow.ps1 -Clean         # Clean build and run tests"
    Write-Host "  .\workflow.ps1 -Verbose       # Run tests with verbose output"
    Write-Host "  .\workflow.ps1 -GenerateSolution  # Generate VS solution files"
    Write-Host "  .\workflow.ps1 -OpenVS        # Generate and open in Visual Studio"
    Write-Host "  .\workflow.ps1 -BuildDevice   # Build device firmware"
    Write-Host "  .\workflow.ps1 -FlashPico    # Build and flash device firmware"
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

# Handle GenerateSolution and OpenVS modes
if ($GenerateSolution -or $OpenVS) {
    if (-not $CurrentIsWindows) {
        Print-Error-Custom "Visual Studio solution generation is only supported on Windows"
        exit 1
    }

    Print-Info "Generating Visual Studio solution files..."

    # Clean build directory for fresh generation
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }

    $CMakeArgs = @("-B", $BuildDir, "-S", $ScriptDir)

    $Generator = "Visual Studio 18 2026"
    $Arch = "x64"
    if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
        $Arch = "ARM64"
    }

    Print-Info "Using Generator: $Generator -A $Arch"
    $CMakeArgs += "-G", $Generator, "-A", $Arch

    & cmake $CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        Print-Error-Custom "CMake configuration failed"
        exit 1
    }

    # Check for .sln (classic) or .slnx (new XML format)
    $SolutionFile = Join-Path $BuildDir "powermonitor.sln"
    $SolutionXFile = Join-Path $BuildDir "powermonitor.slnx"

    $FoundSolution = $null
    if (Test-Path $SolutionXFile) {
        $FoundSolution = $SolutionXFile
        Print-Success "Solution generated (slnx format): $SolutionXFile"
    } elseif (Test-Path $SolutionFile) {
        $FoundSolution = $SolutionFile
        Print-Success "Solution generated: $SolutionFile"
    } else {
        Print-Error-Custom "Solution file not found at $SolutionFile or $SolutionXFile"
        exit 1
    }

    if ($OpenVS) {
        Print-Info "Opening solution in Visual Studio..."
        Start-Process -FilePath "devenv.exe" -ArgumentList $FoundSolution
        Print-Success "Visual Studio launched"
    } else {
        Write-Host ""
        Print-Info "You can now open the solution in Visual Studio:"
        Write-Host "  devenv $FoundSolution"
        Write-Host ""
        Print-Info "Or open directly from File > Open > Project/Solution"
    }

    exit 0
}

# Handle BuildDevice and FlashPico modes
if ($BuildDevice -or $FlashPico) {
    $ShouldFlash = $FlashPico
    if ($BuildDevice -and -not $FlashPico) {
        Print-Info "Building device firmware..."
        Write-Host "======================================"
    } else {
        Print-Info "Building and flashing device firmware..."
        Write-Host "======================================"
    }

    $DeviceDir = Join-Path $ScriptDir "device"
    $DeviceBuildDir = Join-Path $DeviceDir "build"

    # Check for PICO_SDK_PATH
    if (-not $env:PICO_SDK_PATH) {
        Print-Error-Custom "PICO_SDK_PATH environment variable is not set"
        Write-Host ""
        Print-Info "Please set PICO_SDK_PATH to your Pico SDK installation:"
        Write-Host "  Linux/macOS: export PICO_SDK_PATH=/path/to/pico-sdk"
        Write-Host "  Windows (CMD): set PICO_SDK_PATH=C:\path\to\pico-sdk"
        Write-Host "  Windows (PowerShell): `$env:PICO_SDK_PATH = 'C:\path\to\pico-sdk'"
        Write-Host ""
        Write-Host "Clone the Pico SDK:"
        Write-Host "  git clone -b master https://github.com/raspberrypi/pico-sdk.git"
        Write-Host "  cd pico-sdk && git submodule update --init"
        exit 1
    }

    Print-Info "PICO_SDK_PATH: $env:PICO_SDK_PATH"

    # Clean device build if requested
    if ($Clean -or $Rebuild) {
        Print-Info "Cleaning device build directory..."
        if (Test-Path $DeviceBuildDir) {
            Remove-Item -Recurse -Force $DeviceBuildDir
        }
    }

    # Locate Pico SDK bundled tools (.pico-sdk directory from VS Code extension)
    $PicoSdkHome = $null
    $UserHome = $env:USERPROFILE
    if ($UserHome) {
        $Candidate = Join-Path $UserHome ".pico-sdk"
        if (Test-Path $Candidate) {
            $PicoSdkHome = $Candidate
        }
    }

    # Find Ninja from Pico SDK tools
    $NinjaExe = $null
    $PossibleNinjaPaths = @()

    if ($PicoSdkHome) {
        # Search all versioned ninja directories
        $NinjaDir = Join-Path $PicoSdkHome "ninja"
        if (Test-Path $NinjaDir) {
            Get-ChildItem $NinjaDir -Directory | ForEach-Object {
                $PossibleNinjaPaths += (Join-Path $_.FullName "ninja.exe")
            }
        }
    }

    # Also check PATH
    $NinjaInPath = Get-Command ninja -ErrorAction SilentlyContinue
    if ($NinjaInPath) {
        $PossibleNinjaPaths += $NinjaInPath.Source
    }

    foreach ($Path in $PossibleNinjaPaths) {
        if (Test-Path $Path) {
            $NinjaExe = $Path
            break
        }
    }

    # Find CMake from Pico SDK tools (prefer SDK-bundled cmake over system cmake)
    $DeviceCMake = "cmake"
    if ($PicoSdkHome) {
        $CmakeDir = Join-Path $PicoSdkHome "cmake"
        if (Test-Path $CmakeDir) {
            Get-ChildItem $CmakeDir -Directory | ForEach-Object {
                $CmakeCandidate = Join-Path $_.FullName "bin\cmake.exe"
                if (Test-Path $CmakeCandidate) {
                    $DeviceCMake = $CmakeCandidate
                }
            }
        }
    }
    Print-Info "Using CMake: $DeviceCMake"

    # Find ARM toolchain
    $ArmGcc = $null
    if ($PicoSdkHome) {
        $ToolchainDir = Join-Path $PicoSdkHome "toolchain"
        if (Test-Path $ToolchainDir) {
            Get-ChildItem $ToolchainDir -Directory | ForEach-Object {
                $GccCandidate = Join-Path $_.FullName "bin\arm-none-eabi-gcc.exe"
                if (Test-Path $GccCandidate) {
                    $ArmGcc = Join-Path $_.FullName "bin"
                }
            }
        }
    }

    # Configure CMake for device
    $DeviceCMakeCache = Join-Path $DeviceBuildDir "CMakeCache.txt"
    if (-not (Test-Path $DeviceCMakeCache)) {
        Print-Info "Configuring device CMake..."

        Push-Location $DeviceDir
        try {
            $DeviceCMakeArgs = @("-B", $DeviceBuildDir, "-S", ".")

            if ($NinjaExe) {
                Print-Info "Found Ninja: $NinjaExe"
                $DeviceCMakeArgs += "-G", "Ninja"
                $DeviceCMakeArgs += "-DCMAKE_MAKE_PROGRAM=$NinjaExe"
            } else {
                Print-Error-Custom "Ninja not found. The Pico SDK uses Ninja as build system."
                Write-Host "  Option 1: Install Raspberry Pi Pico extension for VS Code (bundles all tools)"
                Write-Host "  Option 2: choco install ninja"
                Write-Host "  Option 3: scoop install ninja"
                exit 1
            }

            # Add ARM toolchain to PATH if found
            if ($ArmGcc) {
                Print-Info "Found ARM toolchain: $ArmGcc"
                $env:PATH = "$ArmGcc;$env:PATH"
            }

            & $DeviceCMake $DeviceCMakeArgs
            if ($LASTEXITCODE -ne 0) {
                Print-Error-Custom "Device CMake configuration failed"
                exit 1
            }
        } finally {
            Pop-Location
        }
        Print-Success "Device CMake configured"
    } else {
        Print-Info "Using existing device CMake configuration"
    }

    # Build device firmware
    Print-Info "Building device firmware..."
    Push-Location $DeviceDir
    try {
        & $DeviceCMake --build $DeviceBuildDir
        if ($LASTEXITCODE -ne 0) {
            Print-Error-Custom "Device build failed"
            exit 1
        }
    } finally {
        Pop-Location
    }

    # Check for output files
    $Uf2File = Join-Path $DeviceBuildDir "powermonitor.uf2"
    if (Test-Path $Uf2File) {
        Print-Success "Device firmware built successfully!"
        Write-Host ""
        Print-Info "Output: $Uf2File"

        if ($ShouldFlash) {
            # Flash the firmware
            Write-Host ""
            Print-Info "Flashing firmware to Pico..."
            Write-Host ""
            Print-Info "Please ensure your Pico is in BOOTSEL mode (hold BOOTSEL while plugging in)"
            Write-Host "Waiting 5 seconds..."
            Start-Sleep -Seconds 5

            # Find the RPI-RP2 drive
            $PicoDrive = $null

            Get-Volume | Where-Object { $_.FileSystemLabel -eq "RPI-RP2" } | ForEach-Object {
                $PicoDrive = $_.DriveLetter
                if ($PicoDrive) {
                    $PicoDrive = $PicoDrive + ":"
                }
            }

            if (-not $PicoDrive) {
                # Try alternative detection via WMI
                $drives = Get-WmiObject -Class Win32_LogicalDisk | Where-Object { $_.VolumeName -eq "RPI-RP2" }
                foreach ($drive in $drives) {
                    $PicoDrive = $drive.DeviceID
                }
            }

            if (-not $PicoDrive) {
                Print-Error-Custom "Could not find Pico in BOOTSEL mode (RPI-RP2 drive not found)"
                Write-Host ""
                Print-Info "Make sure your Pico is connected and in BOOTSEL mode"
                Print-Info "1. Hold BOOTSEL button on Pico"
                Print-Info "2. Connect Pico to USB"
                Print-Info "3. Release BOOTSEL after connection"
                Print-Info "4. The drive should appear as 'RPI-RP2'"
                exit 1
            }

            Print-Info "Found Pico drive: $PicoDrive"

            # Copy the UF2 file
            try {
                Copy-Item -Path $Uf2File -Destination $PicoDrive -ErrorAction Stop
                Print-Success "Firmware flashed successfully!"
                Write-Host ""
                Print-Info "The Pico will reboot and start running the new firmware"
            } catch {
                Print-Error-Custom "Failed to copy firmware to Pico: $_"
                Write-Host ""
                Print-Info "Make sure the RPI-RP2 drive is still accessible"
                exit 1
            }
        } else {
            Write-Host ""
            Print-Info "To flash to Pico:"
            Write-Host "  1. Hold BOOTSEL while plugging in the Pico"
            Write-Host "  2. Copy the .uf2 file to the RPI-RP2 drive"
        }
    } else {
        Print-Warning "Build completed but .uf2 file not found at expected location"
        Print-Info "Check $DeviceBuildDir for output files"
    }

    exit 0
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
