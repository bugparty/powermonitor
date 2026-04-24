#!/usr/bin/env pwsh

# Setup Pico SDK and Toolchain on Linux (Ubuntu)
# Usage: ./setup_pico_sdk.ps1 [-Force]

param (
    [switch]$Force
)

$ErrorActionPreference = "Stop"

# Configuration
$PicoSdkRoot = Join-Path $HOME ".pico-sdk"
$PicoSdkVersion = "2.1.1"
$SdkInstallPath = Join-Path $PicoSdkRoot "sdk/$PicoSdkVersion"

$ToolchainVersion = "13.3.rel1"
$ToolchainUrl = "https://developer.arm.com/-/media/Files/downloads/gnu/$ToolchainVersion/binrel/arm-gnu-toolchain-$ToolchainVersion-x86_64-arm-none-eabi.tar.xz"
$ToolchainArchiveName = "arm-gnu-toolchain-$ToolchainVersion-x86_64-arm-none-eabi.tar.xz"
$ToolchainInstallPath = Join-Path $PicoSdkRoot "toolchain/$ToolchainVersion"

# Colors for output
function Print-Info { param([string]$Message) Write-Host "[INFO] " -ForegroundColor Blue -NoNewline; Write-Host $Message }
function Print-Success { param([string]$Message) Write-Host "[SUCCESS] " -ForegroundColor Green -NoNewline; Write-Host $Message }
function Print-Warning { param([string]$Message) Write-Host "[WARNING] " -ForegroundColor Yellow -NoNewline; Write-Host $Message }
function Print-Error { param([string]$Message) Write-Host "[ERROR] " -ForegroundColor Red -NoNewline; Write-Host $Message }

# Ensure curl/wget/tar exist
if (-not (Get-Command "curl" -ErrorAction SilentlyContinue) -and -not (Get-Command "wget" -ErrorAction SilentlyContinue)) {
    Print-Error "Neither curl nor wget found. Please install one of them."
    exit 1
}
if (-not (Get-Command "tar" -ErrorAction SilentlyContinue)) {
    Print-Error "tar command not found. Please install tar."
    exit 1
}
if (-not (Get-Command "git" -ErrorAction SilentlyContinue)) {
    Print-Error "git command not found. Please install git."
    exit 1
}

# Create directories
if (-not (Test-Path $PicoSdkRoot)) {
    New-Item -ItemType Directory -Path $PicoSdkRoot -Force | Out-Null
    Print-Info "Created directory: $PicoSdkRoot"
}

# 1. Install ARM Toolchain
if ((Test-Path $ToolchainInstallPath) -and (-not $Force)) {
    Print-Success "Toolchain already installed at $ToolchainInstallPath"
} else {
    Print-Info "Installing ARM Toolchain ($ToolchainVersion)..."

    # Download
    $DownloadPath = Join-Path $PicoSdkRoot $ToolchainArchiveName
    if (-not (Test-Path $DownloadPath)) {
        Print-Info "Downloading toolchain from $ToolchainUrl..."
        if (Get-Command "curl" -ErrorAction SilentlyContinue) {
            & curl -L -o $DownloadPath $ToolchainUrl
        } else {
            & wget -O $DownloadPath $ToolchainUrl
        }

        if ($LASTEXITCODE -ne 0) {
            Print-Error "Failed to download toolchain."
            exit 1
        }
    } else {
        Print-Info "Using cached archive: $DownloadPath"
    }

    # Extract
    Print-Info "Extracting toolchain..."
    $ExtractDir = Join-Path $PicoSdkRoot "toolchain"
    New-Item -ItemType Directory -Path $ExtractDir -Force | Out-Null

    # Clean previous extraction if exists
    if (Test-Path $ToolchainInstallPath) {
        Remove-Item -Recurse -Force $ToolchainInstallPath
    }

    # Extract using tar
    # Note: tar usually extracts into a folder named after the archive basename
    # We want to rename/move it to our target path

    # Create temp dir for extraction
    $TempExtract = Join-Path $PicoSdkRoot "temp_extract"
    New-Item -ItemType Directory -Path $TempExtract -Force | Out-Null

    & tar -xf $DownloadPath -C $TempExtract
    if ($LASTEXITCODE -ne 0) {
        Print-Error "Failed to extract toolchain."
        exit 1
    }

    # Find the extracted folder
    $ExtractedFolder = Get-ChildItem -Path $TempExtract | Select-Object -First 1
    if (-not $ExtractedFolder) {
        Print-Error "Extraction failed: No folder found."
        exit 1
    }

    # Move to final location
    Move-Item -Path $ExtractedFolder.FullName -Destination $ToolchainInstallPath -Force
    Remove-Item -Recurse -Force $TempExtract

    # Cleanup archive
    # Remove-Item -Force $DownloadPath # Keep checking to save bandwidth if run again

    Print-Success "Toolchain installed to $ToolchainInstallPath"
}

# 2. Install Pico SDK
if ((Test-Path $SdkInstallPath) -and (-not $Force)) {
    Print-Success "Pico SDK already installed at $SdkInstallPath"
} else {
    Print-Info "Installing Pico SDK ($PicoSdkVersion)..."

    $SdkParent = Split-Path -Parent $SdkInstallPath
    New-Item -ItemType Directory -Path $SdkParent -Force | Out-Null

    Print-Info "Cloning Pico SDK..."
    & git clone --depth 1 --branch $PicoSdkVersion https://github.com/raspberrypi/pico-sdk.git $SdkInstallPath
    if ($LASTEXITCODE -ne 0) {
        Print-Error "Failed to clone Pico SDK."
        exit 1
    }

    Print-Info "Initializing submodules..."
    Push-Location $SdkInstallPath
    & git submodule update --init
    Pop-Location

    Print-Success "Pico SDK installed to $SdkInstallPath"
}

# 3. Create Environment Script
$EnvFilePs1 = Join-Path $PicoSdkRoot "env.ps1"
$EnvFileSh = Join-Path $PicoSdkRoot "env.sh"

$BinPath = Join-Path $ToolchainInstallPath "bin"

# PS1
$ContentPs1 = @"
`$env:PICO_SDK_PATH = "$SdkInstallPath"
`$env:PATH = "$BinPath" + [IO.Path]::PathSeparator + `$env:PATH
Write-Host "Pico SDK environment configured." -ForegroundColor Green
"@
Set-Content -Path $EnvFilePs1 -Value $ContentPs1

# SH
$ContentSh = @"
export PICO_SDK_PATH="$SdkInstallPath"
export PATH="${BinPath}:`$PATH"
echo "Pico SDK environment configured."
"@
Set-Content -Path $EnvFileSh -Value $ContentSh

# Make .sh executable
if ($IsLinux) {
    & chmod +x $EnvFileSh
}

Print-Success "Environment scripts created."
Write-Host ""
Print-Info "To use the environment in PowerShell, run:"
Write-Host "  . $EnvFilePs1"
Write-Host ""
Print-Info "To use the environment in Bash, run:"
Write-Host "  source $EnvFileSh"
Write-Host ""
Print-Info "You can now build the device firmware!"
