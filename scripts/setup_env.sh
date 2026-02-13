#!/bin/bash

# Power Monitor Agent Environment Setup Script
# This script installs all dependencies needed for development and testing

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

print_info "Setting up Power Monitor development environment..."
echo "======================================"

# Detect OS
if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    OS=$ID
    OS_VERSION=$VERSION_ID
else
    print_error "Cannot detect OS. This script supports Ubuntu/Debian."
    exit 1
fi

print_info "Detected OS: $OS $OS_VERSION"

# Update package list
print_info "Updating package list..."
sudo apt update

# Install base dependencies
print_info "Installing base dependencies..."
sudo apt install -y \
    wget \
    apt-transport-https \
    software-properties-common \
    curl \
    git \
    build-essential \
    cmake \
    ninja-build

# Verify CMake version
CMAKE_VERSION=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d. -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d. -f2)

print_info "CMake version: $CMAKE_VERSION"

if [[ $CMAKE_MAJOR -lt 3 ]] || [[ $CMAKE_MAJOR -eq 3 && $CMAKE_MINOR -lt 13 ]]; then
    print_warning "CMake version $CMAKE_VERSION is older than required 3.13"
    print_info "Installing newer CMake from Kitware repository..."

    # Remove old CMake if installed from apt
    sudo apt remove --purge --auto-remove cmake || true

    # Install Kitware's official CMake repository
    sudo apt install -y apt-transport-https ca-certificates gnupg software-properties-common wget
    wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | sudo gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
    sudo apt update
    sudo apt install -y cmake

    CMAKE_VERSION=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)
    print_success "Updated CMake to version $CMAKE_VERSION"
fi

# Install PowerShell
print_info "Installing PowerShell..."
if ! command -v pwsh &> /dev/null; then
    # Download and install Microsoft repository
    wget -q "https://packages.microsoft.com/config/ubuntu/$(lsb_release -rs)/packages-microsoft-prod.deb" -O packages-microsoft-prod.deb
    sudo dpkg -i packages-microsoft-prod.deb
    rm -f packages-microsoft-prod.deb
    sudo apt update
    sudo apt install -y powershell
    print_success "PowerShell installed successfully"
else
    print_success "PowerShell is already installed"
fi

# Install C++ development tools (if not already present)
print_info "Checking C++ compiler..."
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    print_info "Installing GCC..."
    sudo apt install -y gcc g++
fi

# Verify C++17 support
print_info "Verifying C++17 support..."
if command -v g++ &> /dev/null; then
    GCC_VERSION=$(g++ --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1 | cut -d. -f1)
    if [[ $GCC_VERSION -ge 7 ]]; then
        print_success "GCC version $(g++ --version | head -n1) supports C++17"
    else
        print_warning "GCC version may not fully support C++17. Consider upgrading."
    fi
elif command -v clang++ &> /dev/null; then
    CLANG_VERSION=$(clang++ --version | head -n1 | grep -oE 'version [0-9]+' | grep -oE '[0-9]+')
    if [[ $CLANG_VERSION -ge 5 ]]; then
        print_success "Clang version $(clang++ --version | head -n1) supports C++17"
    else
        print_warning "Clang version may not fully support C++17. Consider upgrading."
    fi
fi

# Install additional useful tools
print_info "Installing additional development tools..."
sudo apt install -y \
    gdb \
    valgrind \
    cppcheck \
    clang-format \
    clang-tidy \
    doxygen

echo ""
echo "======================================"
print_success "Environment setup complete!"
echo ""
print_info "Installed components:"
echo "  - CMake $(cmake --version | head -n1 | awk '{print $3}')"
echo "  - $(g++ --version | head -n1)"
echo "  - PowerShell $(pwsh --version 2>/dev/null || echo 'N/A')"
echo "  - Git $(git --version | awk '{print $3}')"
echo ""
print_info "You can now build and test the project:"
echo "  ./test.sh          # Run all tests"
echo "  ./test.sh --clean  # Clean build and run tests"
echo ""
print_info "For device firmware development, install Pico SDK:"
echo "  https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf"
