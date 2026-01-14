
#!/bin/bash
set -e

# SpeedyNote Linux Compilation Script

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check if we're in the right directory
check_project_directory() {
    if [[ ! -f "CMakeLists.txt" ]]; then
        echo -e "${RED}Error: This doesn't appear to be the SpeedyNote project directory${NC}"
        echo "Please run this script from the SpeedyNote project root directory"
        exit 1
    fi
}

# Function to detect architecture
detect_architecture() {
    local arch=$(uname -m)
    case $arch in
        x86_64|amd64)
            echo "x86-64"
            ;;
        aarch64|arm64)
            echo "ARM64"
            ;;
        *)
            echo "Unknown ($arch)"
            ;;
    esac
}

# Function to build the project
build_project() {
    local debug_flag="OFF"
    for arg in "$@"; do
        if [[ "$arg" == "--debug" || "$arg" == "-debug" ]]; then
            debug_flag="ON"
        fi
    done

    echo -e "${YELLOW}Building SpeedyNote...${NC}"
    
    # Detect and display architecture
    local arch_type=$(detect_architecture)
    echo -e "${CYAN}Detected architecture: ${arch_type}${NC}"
    
    case $arch_type in
        "x86-64")
            echo -e "${CYAN}Optimization target: 1st gen Intel Core i (Nehalem) with SSE4.2${NC}"
            ;;
        "ARM64")
            echo -e "${CYAN}Optimization target: Cortex-A72/A53 (ARMv8-A with CRC32)${NC}"
            ;;
        *)
            echo -e "${YELLOW}Using generic optimizations${NC}"
            ;;
    esac

    if [[ "$debug_flag" == "ON" ]]; then
        echo -e "${YELLOW}Debug output: ENABLED${NC}"
    else
        echo -e "${CYAN}Debug output: DISABLED${NC}"
    fi
    
    # Clean and create build directory
    rm -rf build
    mkdir -p build
    
    # Compile translations if lrelease is available
    if command_exists lrelease; then
        echo -e "${YELLOW}Compiling translation files...${NC}"
        # lrelease ./resources/translations/app_zh.ts ./resources/translations/app_fr.ts ./resources/translations/app_es.ts
        cp resources/translations/*.qm build/ 2>/dev/null || true
    fi
    
    cd build
    
    # Configure and build with optimizations
    echo -e "${YELLOW}Configuring build with maximum performance optimizations...${NC}"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DENABLE_DEBUG_OUTPUT=$debug_flag ..
    
    echo -e "${YELLOW}Compiling with $(nproc) parallel jobs...${NC}"
    make -j$(nproc)
    
    if [[ ! -f "NoteApp" ]]; then
        echo -e "${RED}Build failed: NoteApp executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}Build successful!${NC}"
}

# Main execution
main() {
    echo -e "${BLUE}Starting SpeedyNote compilation process...${NC}"
    
    # Step 1: Verify environment
    check_project_directory
    
    # Step 2: Build project
    build_project "$@"
    
    echo
    echo -e "${GREEN}SpeedyNote compilation completed successfully!${NC}"
}

# Run main function
main "$@"
