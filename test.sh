#!/bin/bash

# Power Monitor Test Runner
# This script builds and runs the Google Test suite

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

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/linuxbuild"

print_info "Power Monitor Test Suite"
echo "======================================"

# Parse command line arguments
CLEAN_BUILD=0
VERBOSE=0
REBUILD=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --rebuild)
            REBUILD=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --clean      Clean build directory before building"
            echo "  --rebuild    Rebuild all targets (equivalent to --clean)"
            echo "  -v, --verbose   Verbose output from tests"
            echo "  -h, --help   Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Run tests with existing build"
            echo "  $0 --clean            # Clean build and run tests"
            echo "  $0 --verbose          # Run tests with verbose output"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Clean build if requested
if [[ $CLEAN_BUILD -eq 1 ]] || [[ $REBUILD -eq 1 ]]; then
    print_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Configure CMake
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    print_info "Configuring CMake..."
    cmake -B "$BUILD_DIR" -S "$SCRIPT_DIR"
    if [[ $? -ne 0 ]]; then
        print_error "CMake configuration failed"
        exit 1
    fi
    print_success "CMake configured"
else
    print_info "Using existing CMake configuration"
fi

# Build
print_info "Building project..."
cmake --build "$BUILD_DIR"
if [[ $? -ne 0 ]]; then
    print_error "Build failed"
    exit 1
fi
print_success "Build completed"

# Check if test executable exists
TEST_EXEC="$BUILD_DIR/pc_sim/pc_sim_test"
if [[ ! -f "$TEST_EXEC" ]]; then
    print_error "Test executable not found: $TEST_EXEC"
    exit 1
fi

echo ""
print_info "Running tests..."
echo "======================================"

# Run tests
if [[ $VERBOSE -eq 1 ]]; then
    "$TEST_EXEC" --gtest_color=yes
else
    "$TEST_EXEC" --gtest_color=yes --gtest_brief=1
fi

TEST_RESULT=$?

echo "======================================"

if [[ $TEST_RESULT -eq 0 ]]; then
    print_success "All tests passed! ✓"
    exit 0
else
    print_error "Some tests failed! ✗"
    echo ""
    print_info "Tip: Run with --verbose flag for detailed output"
    exit 1
fi
