#!/bin/bash

# Power Monitor Agent Environment Setup Script (No Root Required)
# This script installs all dependencies to ~/.local without requiring sudo

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored message
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Resolve target user/home even when script is invoked via sudo.
TARGET_USER="$USER"
TARGET_HOME="$HOME"

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    if [[ -n "${SUDO_USER:-}" ]] && [[ "$SUDO_USER" != "root" ]]; then
        TARGET_USER="$SUDO_USER"
        TARGET_HOME="$(eval echo "~$SUDO_USER")"
    else
        print_error "setup_env_noroot.sh should be run as a non-root user."
        print_error "Current user is root and SUDO_USER is not set."
        print_error "Please switch to your normal user account and re-run this script."
        exit 1
    fi
fi

# Installation directories
LOCAL_PREFIX="$TARGET_HOME/.local"
LOCAL_BIN="$LOCAL_PREFIX/bin"
LOCAL_LIB="$LOCAL_PREFIX/lib"
LOCAL_INCLUDE="$LOCAL_PREFIX/include"
LOCAL_SHARE="$LOCAL_PREFIX/share"
LOCAL_TMP="$TARGET_HOME/.cache/powermonitor-setup"

# Architecture detection
ARCH=$(uname -m)
case $ARCH in
    x86_64)  ARCH_STR="x86_64" ;;
    aarch64) ARCH_STR="aarch64" ;;
    armv7l)  ARCH_STR="armv7l" ;;
    *)       ARCH_STR="$ARCH" ;;
esac

print_info "Setting up Power Monitor development environment (no-root mode)..."
echo "======================================"
print_info "Target user: $TARGET_USER"
print_info "Installation prefix: $LOCAL_PREFIX"
print_info "Architecture: $ARCH_STR"

# Create directories
mkdir -p "$LOCAL_PREFIX" "$LOCAL_BIN" "$LOCAL_LIB" "$LOCAL_INCLUDE" "$LOCAL_SHARE" "$LOCAL_TMP"

# Check if ~/.local/bin is in PATH
if [[ ":$PATH:" != *":$LOCAL_BIN:"* ]]; then
    print_warning "$LOCAL_BIN is not in PATH"
    print_info "Adding to PATH for this session..."
    export PATH="$LOCAL_BIN:$PATH"

    # Add to shell config
    SHELL_RC=""
    if [[ -f "$TARGET_HOME/.bashrc" ]]; then
        SHELL_RC="$TARGET_HOME/.bashrc"
    elif [[ -f "$TARGET_HOME/.zshrc" ]]; then
        SHELL_RC="$TARGET_HOME/.zshrc"
    fi

    if [[ -n "$SHELL_RC" ]]; then
        if ! grep -q 'export PATH="$HOME/.local/bin:$PATH"' "$SHELL_RC" 2>/dev/null; then
            echo '' >> "$SHELL_RC"
            echo '# Added by powermonitor setup script' >> "$SHELL_RC"
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$SHELL_RC"
            print_info "Added ~/.local/bin to $SHELL_RC"
        fi
    fi
fi

# If invoked via sudo, restore ownership of touched files to the target user.
fix_target_ownership() {
    if [[ ${EUID:-$(id -u)} -eq 0 ]] && [[ "$TARGET_USER" != "root" ]]; then
        chown -R "$TARGET_USER":"$TARGET_USER" "$LOCAL_PREFIX" "$TARGET_HOME/.cache/powermonitor-setup" 2>/dev/null || true
        chown "$TARGET_USER":"$TARGET_USER" "$TARGET_HOME/.bashrc" "$TARGET_HOME/.zshrc" 2>/dev/null || true
    fi
}

# Function to check if command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Function to get latest CMake version from GitHub
get_latest_cmake_version() {
    curl -s "https://api.github.com/repos/Kitware/CMake/releases/latest" | grep -oP '"tag_name":\s*"v\K[^"]+' | head -1
}

# Install CMake
install_cmake() {
    print_info "Installing CMake..."

    if command_exists cmake; then
        CMAKE_VERSION=$(cmake --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)
        CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
        CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)

        if [[ $CMAKE_MAJOR -ge 3 ]] && [[ $CMAKE_MINOR -ge 13 ]]; then
            print_success "CMake $CMAKE_VERSION already installed and meets requirements"
            return 0
        else
            print_warning "System CMake $CMAKE_VERSION is too old, installing newer version..."
        fi
    fi

    # Determine CMake version and download URL
    CMAKE_VERSION=$(get_latest_cmake_version)
    if [[ -z "$CMAKE_VERSION" ]]; then
        CMAKE_VERSION="3.28.3"  # Fallback version
    fi

    print_info "Downloading CMake $CMAKE_VERSION..."

    # Determine OS and architecture for download
    OS_NAME="linux"
    CMAKE_ARCH="$ARCH_STR"

    case "$ARCH_STR" in
        x86_64)  CMAKE_ARCH="x86_64" ;;
        aarch64) CMAKE_ARCH="aarch64" ;;
        *)       CMAKE_ARCH="$ARCH_STR" ;;
    esac

    CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-${OS_NAME}-${CMAKE_ARCH}.sh"
    CMAKE_INSTALLER="$LOCAL_TMP/cmake-installer.sh"

    if ! curl -fsSL "$CMAKE_URL" -o "$CMAKE_INSTALLER"; then
        print_error "Failed to download CMake from $CMAKE_URL"
        return 1
    fi

    print_info "Installing CMake to $LOCAL_PREFIX..."
    chmod +x "$CMAKE_INSTALLER"
    "$CMAKE_INSTALLER" --skip-license --prefix="$LOCAL_PREFIX" --exclude-subdir

    rm -f "$CMAKE_INSTALLER"

    if command_exists cmake; then
        print_success "CMake $(cmake --version | head -n1 | awk '{print $3}') installed successfully"
    else
        print_error "CMake installation failed"
        return 1
    fi
}

# Install Ninja (optional but faster than make)
install_ninja() {
    print_info "Installing Ninja..."

    if command_exists ninja; then
        print_success "Ninja already installed: $(ninja --version)"
        return 0
    fi

    NINJA_URL="https://github.com/ninja-build/ninja/releases/latest/download/ninja-${ARCH_STR}.zip"
    NINJA_ZIP="$LOCAL_TMP/ninja.zip"

    if ! curl -fsSL "$NINJA_URL" -o "$NINJA_ZIP"; then
        print_warning "Failed to download Ninja, skipping..."
        return 0
    fi

    cd "$LOCAL_TMP"
    unzip -o "$NINJA_ZIP" -d "$LOCAL_BIN"
    chmod +x "$LOCAL_BIN/ninja"
    rm -f "$NINJA_ZIP"
    cd - > /dev/null

    if command_exists ninja; then
        print_success "Ninja $(ninja --version) installed successfully"
    fi
}

# Install PowerShell
install_powershell() {
    print_info "Installing PowerShell..."

    OS_NAME="linux"

    if command_exists pwsh; then
        print_success "PowerShell already installed: $(pwsh --version 2>/dev/null || echo 'unknown version')"
        return 0
    fi

    # Download PowerShell
    POWSH_VERSION="7.4.1"
    PWSH_URL="https://github.com/PowerShell/PowerShell/releases/download/v${POWSH_VERSION}/powershell-${POWSH_VERSION}-${OS_NAME}-${ARCH_STR}.tar.gz"
    PWSH_DIR="$LOCAL_SHARE/powershell"
    PWSH_TAR="$LOCAL_TMP/powershell.tar.gz"

    case "$ARCH_STR" in
        x86_64)  PWSH_ARCH="x64" ;;
        aarch64) PWSH_ARCH="arm64" ;;
        armv7l)  PWSH_ARCH="arm32" ;;
        *)       PWSH_ARCH="$ARCH_STR" ;;
    esac

    PWSH_URL="https://github.com/PowerShell/PowerShell/releases/download/v${POWSH_VERSION}/powershell-${POWSH_VERSION}-${OS_NAME}-${POWSH_ARCH}.tar.gz"

    print_info "Downloading PowerShell from $POWSH_URL..."

    if ! curl -fsSL "$POWSH_URL" -o "$POWSH_TAR"; then
        print_warning "Failed to download PowerShell, skipping..."
        return 0
    fi

    mkdir -p "$POWSH_DIR"
    tar -xzf "$POWSH_TAR" -C "$POWSH_DIR"
    chmod +x "$POWSH_DIR/pwsh"
    ln -sf "$POWSH_DIR/pwsh" "$LOCAL_BIN/pwsh"
    rm -f "$POWSH_TAR"

    if command_exists pwsh; then
        print_success "PowerShell installed successfully"
    fi
}

# Check GCC/Clang for C++17 support
check_cpp_compiler() {
    print_info "Checking C++ compiler..."

    if command_exists g++; then
        GCC_VERSION=$(g++ -dumpversion | cut -d. -f1)
        if [[ $GCC_VERSION -ge 7 ]]; then
            print_success "GCC $(g++ -dumpversion) supports C++17"
            return 0
        else
            print_warning "GCC version $(g++ -dumpversion) may not fully support C++17"
        fi
    fi

    if command_exists clang++; then
        CLANG_VERSION=$(clang++ -dumpversion | cut -d. -f1)
        if [[ $CLANG_VERSION -ge 5 ]]; then
            print_success "Clang $(clang++ -dumpversion) supports C++17"
            return 0
        fi
    fi

    print_warning "No suitable C++17 compiler found"
    print_info "Please install GCC 7+ or Clang 5+ using your system package manager"
    print_info "  Ubuntu/Debian: sudo apt install g++-7  (or newer)"
    print_info "  CentOS/RHEL:   sudo yum install gcc-toolset-7"
    print_info "  Fedora:        sudo dnf install gcc-c++"

    return 1
}

# Install Git if not present (usually available)
check_git() {
    print_info "Checking Git..."

    if command_exists git; then
        print_success "Git $(git --version | awk '{print $3}') installed"
        return 0
    else
        print_warning "Git not found. Please install it using your system package manager."
        return 1
    fi
}

# Setup environment file for sourcing
setup_env_file() {
    ENV_FILE="$LOCAL_PREFIX/powersetup.env"

    cat > "$ENV_FILE" << 'EOF'
# Power Monitor Development Environment
# Source this file: source ~/.local/powersetup.env

export PATH="$HOME/.local/bin:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/lib:$LD_LIBRARY_PATH"
export CMAKE_PREFIX_PATH="$HOME/.local:$CMAKE_PREFIX_PATH"
EOF

    print_info "Environment file created: $ENV_FILE"
    print_info "Add 'source $ENV_FILE' to your shell config or run it manually"
}

# Main installation
main() {
    echo ""

    # Check prerequisites
    check_git || true

    # Install tools
    install_cmake
    install_ninja
    check_cpp_compiler || true
    install_powershell

    # Setup environment
    setup_env_file
    fix_target_ownership

    echo ""
    echo "======================================"
    print_success "Environment setup complete!"
    echo ""
    print_info "Installed components:"
    echo "  - CMake: $(cmake --version 2>/dev/null | head -n1 | awk '{print $3}' || echo 'not installed')"
    echo "  - Ninja: $(ninja --version 2>/dev/null || echo 'not installed')"
    echo "  - PowerShell: $(pwsh --version 2>/dev/null || echo 'not installed')"
    echo "  - Git: $(git --version 2>/dev/null | awk '{print $3}' || echo 'not installed')"
    echo ""
    print_info "Installation prefix: $LOCAL_PREFIX"
    echo ""
    print_info "To use the installed tools in future sessions, add to your shell config:"
    echo "  source $LOCAL_PREFIX/powersetup.env"
    echo ""
    print_info "Or add this line to ~/.bashrc or ~/.zshrc:"
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
    print_info "You can now build and test the project:"
    echo "  pwsh workflow.ps1              # Run all tests"
    echo "  pwsh workflow.ps1 -Clean      # Clean build and run tests"
}

# Run main
main "$@"
