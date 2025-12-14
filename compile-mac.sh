#!/bin/bash
set -e

# SpeedyNote macOS Compilation Script

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
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
            echo "Intel x86-64"
            ;;
        arm64|aarch64)
            echo "Apple Silicon (ARM64)"
            ;;
        *)
            echo "Unknown ($arch)"
            ;;
    esac
}

# Function to check Homebrew installation
check_homebrew() {
    echo -e "${YELLOW}Checking Homebrew installation...${NC}"
    
    if ! command_exists brew; then
        echo -e "${RED}Error: Homebrew is not installed${NC}"
        echo "Please install Homebrew from https://brew.sh"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Homebrew found${NC}"
}

# Function to check and install dependencies
check_dependencies() {
    echo -e "${YELLOW}Checking required dependencies...${NC}"
    
    local missing_deps=()
    
    # Check for required packages
    if ! brew list qt@6 &>/dev/null; then
        missing_deps+=("qt@6")
    fi
    
    if ! brew list poppler &>/dev/null; then
        missing_deps+=("poppler")
    fi
    
    if ! brew list sdl2 &>/dev/null; then
        missing_deps+=("sdl2")
    fi
    
    if ! brew list cmake &>/dev/null; then
        missing_deps+=("cmake")
    fi
    
    if ! brew list pkg-config &>/dev/null; then
        missing_deps+=("pkg-config")
    fi
    
    if [ ${#missing_deps[@]} -eq 0 ]; then
        echo -e "${GREEN}✓ All dependencies are installed${NC}"
    else
        echo -e "${YELLOW}Missing dependencies: ${missing_deps[*]}${NC}"
        echo -e "${CYAN}Would you like to install them now? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            echo -e "${YELLOW}Installing missing dependencies...${NC}"
            brew install "${missing_deps[@]}"
            echo -e "${GREEN}✓ Dependencies installed${NC}"
        else
            echo -e "${RED}Cannot proceed without required dependencies${NC}"
            exit 1
        fi
    fi
}

# Function to set up Qt paths
setup_qt_paths() {
    local arch=$(uname -m)
    
    if [[ "$arch" == "arm64" ]] || [[ "$arch" == "aarch64" ]]; then
        export HOMEBREW_PREFIX="/opt/homebrew"
    else
        export HOMEBREW_PREFIX="/usr/local"
    fi
    
    # Add Qt binaries to PATH for lrelease
    export PATH="${HOMEBREW_PREFIX}/opt/qt@6/bin:$PATH"
    
    echo -e "${CYAN}Using Homebrew prefix: ${HOMEBREW_PREFIX}${NC}"
}

# Function to build the project
build_project() {
    echo -e "${YELLOW}Building SpeedyNote...${NC}"
    
    # Detect and display architecture
    local arch_type=$(detect_architecture)
    echo -e "${CYAN}Detected architecture: ${arch_type}${NC}"
    
    case $arch_type in
        "Apple Silicon (ARM64)")
            echo -e "${MAGENTA}🍎 Optimization target: Apple Silicon (M1/M2/M3/M4)${NC}"
            ;;
        "Intel x86-64")
            echo -e "${CYAN}🍎 Optimization target: Intel Mac (Nehalem/Core i series)${NC}"
            ;;
        *)
            echo -e "${YELLOW}Using generic optimizations${NC}"
            ;;
    esac
    
    # Clean and create build directory
    rm -rf build
    mkdir -p build
    
    # Compile translations if lrelease is available
    if command_exists lrelease; then
        echo -e "${YELLOW}Compiling translation files...${NC}"
        lrelease ./resources/translations/app_zh.ts ./resources/translations/app_fr.ts ./resources/translations/app_es.ts 2>/dev/null || true
        cp resources/translations/*.qm build/ 2>/dev/null || true
    fi
    
    cd build
    
    # Configure and build with optimizations
    echo -e "${YELLOW}Configuring build with maximum performance optimizations...${NC}"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
    
    # Get CPU count for parallel compilation
    local cpu_count=$(sysctl -n hw.ncpu)
    echo -e "${YELLOW}Compiling with ${cpu_count} parallel jobs...${NC}"
    make -j${cpu_count}
    
    if [[ ! -f "NoteApp" ]]; then
        echo -e "${RED}Build failed: NoteApp executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}Build successful!${NC}"
    echo -e "${CYAN}Executable location: build/NoteApp${NC}"
}

# Function to bundle custom libraries (poppler-qt6, SDL2)
bundle_custom_libraries() {
    local app_path="$1"
    local executable="$app_path/Contents/MacOS/NoteApp"
    local frameworks_dir="$app_path/Contents/Frameworks"
    
    mkdir -p "$frameworks_dir"
    
    echo -e "${YELLOW}Bundling custom libraries...${NC}"
    
    # Bundle poppler-qt6 (custom compiled in /opt/poppler-qt6)
    if [[ -d "/opt/poppler-qt6/lib" ]]; then
        echo -e "${CYAN}  → Copying poppler-qt6 libraries...${NC}"
        
        # Copy all poppler libraries (including versioned ones)
        cp /opt/poppler-qt6/lib/libpoppler-qt6*.dylib "$frameworks_dir/" 2>/dev/null || true
        cp /opt/poppler-qt6/lib/libpoppler.*.dylib "$frameworks_dir/" 2>/dev/null || true
        cp /opt/poppler-qt6/lib/libpoppler-cpp*.dylib "$frameworks_dir/" 2>/dev/null || true
        
        # List what we copied for debugging
        echo -e "${CYAN}  → Copied libraries:${NC}"
        ls -1 "$frameworks_dir"/libpoppler*.dylib 2>/dev/null | sed 's/.*\//    /' || echo "    (none found)"
        
        # Fix library IDs and dependencies
        for lib in "$frameworks_dir"/libpoppler*.dylib; do
            if [[ -f "$lib" ]]; then
                local libname=$(basename "$lib")
                
                # Step 1: Set the library's own install name
                install_name_tool -id "@loader_path/$libname" "$lib" 2>/dev/null || true
                
                # Step 2: Fix all poppler dependencies within this library
                # This handles cases like libpoppler-qt6 depending on libpoppler
                for dep in /opt/poppler-qt6/lib/libpoppler*.dylib; do
                    if [[ -f "$dep" ]]; then
                        local depname=$(basename "$dep")
                        # Change absolute paths
                        install_name_tool -change "/opt/poppler-qt6/lib/$depname" \
                            "@loader_path/$depname" "$lib" 2>/dev/null || true
                        # Change @rpath references
                        install_name_tool -change "@rpath/$depname" \
                            "@loader_path/$depname" "$lib" 2>/dev/null || true
                    fi
                done
            fi
        done
        
        # Update executable to use bundled poppler libraries
        for lib in "$frameworks_dir"/libpoppler*.dylib; do
            if [[ -f "$lib" ]]; then
                local libname=$(basename "$lib")
                # Fix references from executable
                install_name_tool -change "@rpath/$libname" \
                    "@executable_path/../Frameworks/$libname" "$executable" 2>/dev/null || true
                install_name_tool -change "/opt/poppler-qt6/lib/$libname" \
                    "@executable_path/../Frameworks/$libname" "$executable" 2>/dev/null || true
            fi
        done
        
        echo -e "${GREEN}  ✓ poppler-qt6 bundled and relinked${NC}"
    fi
    
    # Bundle SDL2 (from /usr/local/lib)
    if [[ -f "/usr/local/lib/libSDL2-2.0.0.dylib" ]]; then
        echo -e "${CYAN}  → Copying SDL2 library...${NC}"
        cp /usr/local/lib/libSDL2-2.0.0.dylib "$frameworks_dir/"
        cp /usr/local/lib/libSDL2.dylib "$frameworks_dir/" 2>/dev/null || true
        
        # Fix SDL2 library ID
        install_name_tool -id "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" \
            "$frameworks_dir/libSDL2-2.0.0.dylib" 2>/dev/null || true
        
        # Update executable to use bundled SDL2
        install_name_tool -change "/usr/local/opt/sdl2/lib/libSDL2-2.0.0.dylib" \
            "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" "$executable" 2>/dev/null || true
        install_name_tool -change "/usr/local/lib/libSDL2-2.0.0.dylib" \
            "@executable_path/../Frameworks/libSDL2-2.0.0.dylib" "$executable" 2>/dev/null || true
        
        echo -e "${GREEN}  ✓ SDL2 bundled${NC}"
    fi
    
    # Remove rpaths since we're now using @executable_path
    install_name_tool -delete_rpath "/opt/poppler-qt6/lib" "$executable" 2>/dev/null || true
    install_name_tool -delete_rpath "/usr/local/opt/qt@6/lib" "$executable" 2>/dev/null || true
    
    echo -e "${GREEN}✓ Custom libraries bundled successfully${NC}"
}

# Function to create app bundle (optional)
create_app_bundle() {
    echo
    echo -e "${CYAN}Would you like to create a macOS .app bundle? (y/n)${NC}"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        echo -e "${YELLOW}Creating SpeedyNote.app bundle...${NC}"
        
        # Clean up any existing bundle
        rm -rf "SpeedyNote.app"
        
        # Create app bundle structure
        mkdir -p "SpeedyNote.app/Contents/MacOS"
        mkdir -p "SpeedyNote.app/Contents/Resources"
        mkdir -p "SpeedyNote.app/Contents/Frameworks"
        
        # Copy executable
        cp build/NoteApp "SpeedyNote.app/Contents/MacOS/"
        
        # Create and copy macOS icon (.icns)
        echo -e "${YELLOW}Creating macOS icon...${NC}"
        if [[ -f "resources/icons/mainicon.png" ]]; then
            # Create iconset directory
            local iconset_dir="SpeedyNote.iconset"
            rm -rf "$iconset_dir"
            mkdir -p "$iconset_dir"
            
            # Generate all required icon sizes using sips
            # macOS requires specific sizes for .icns
            sips -z 16 16     resources/icons/mainicon.png --out "$iconset_dir/icon_16x16.png" >/dev/null 2>&1
            sips -z 32 32     resources/icons/mainicon.png --out "$iconset_dir/icon_16x16@2x.png" >/dev/null 2>&1
            sips -z 32 32     resources/icons/mainicon.png --out "$iconset_dir/icon_32x32.png" >/dev/null 2>&1
            sips -z 64 64     resources/icons/mainicon.png --out "$iconset_dir/icon_32x32@2x.png" >/dev/null 2>&1
            sips -z 128 128   resources/icons/mainicon.png --out "$iconset_dir/icon_128x128.png" >/dev/null 2>&1
            sips -z 256 256   resources/icons/mainicon.png --out "$iconset_dir/icon_128x128@2x.png" >/dev/null 2>&1
            sips -z 256 256   resources/icons/mainicon.png --out "$iconset_dir/icon_256x256.png" >/dev/null 2>&1
            sips -z 512 512   resources/icons/mainicon.png --out "$iconset_dir/icon_256x256@2x.png" >/dev/null 2>&1
            sips -z 512 512   resources/icons/mainicon.png --out "$iconset_dir/icon_512x512.png" >/dev/null 2>&1
            sips -z 1024 1024 resources/icons/mainicon.png --out "$iconset_dir/icon_512x512@2x.png" >/dev/null 2>&1
            
            # Convert iconset to .icns
            iconutil -c icns "$iconset_dir" -o "SpeedyNote.app/Contents/Resources/AppIcon.icns" 2>/dev/null
            
            if [[ -f "SpeedyNote.app/Contents/Resources/AppIcon.icns" ]]; then
                echo -e "${GREEN}  ✓ Icon created (AppIcon.icns)${NC}"
                rm -rf "$iconset_dir"
            else
                echo -e "${YELLOW}  ⚠ Failed to create .icns, copying PNG as fallback${NC}"
                cp "resources/icons/mainicon.png" "SpeedyNote.app/Contents/Resources/"
                rm -rf "$iconset_dir"
            fi
        else
            echo -e "${YELLOW}  ⚠ Icon not found at resources/icons/mainicon.png${NC}"
        fi
        
        # Extract version from CMakeLists.txt
        local VERSION=$(grep "project(SpeedyNote VERSION" CMakeLists.txt | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')
        if [[ -z "$VERSION" ]]; then
            VERSION="0.12.1"
        fi
        
        # Create Info.plist
        cat > "SpeedyNote.app/Contents/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>NoteApp</string>
    <key>CFBundleIdentifier</key>
    <string>com.github.alpha-liu-01.SpeedyNote</string>
    <key>CFBundleName</key>
    <string>SpeedyNote</string>
    <key>CFBundleDisplayName</key>
    <string>SpeedyNote</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
</dict>
</plist>
EOF
        
        echo
        echo -e "${CYAN}═══════════════════════════════════════════════${NC}"
        echo -e "${CYAN}  Bundle Configuration${NC}"
        echo -e "${CYAN}═══════════════════════════════════════════════${NC}"
        echo
        echo -e "${YELLOW}Choose bundle type:${NC}"
        echo -e "  ${GREEN}1)${NC} Development bundle"
        echo -e "     - Smallest size (~2MB)"
        echo -e "     - Links to system libraries"
        echo -e "     - Requires: Qt, poppler, SDL2 installed"
        echo -e "     - ${CYAN}Use this for development${NC}"
        echo
        echo -e "  ${GREEN}2)${NC} Portable bundle ${MAGENTA}⭐ RECOMMENDED${NC}"
        echo -e "     - Medium size (~20MB)"
        echo -e "     - Embeds poppler-qt6 and SDL2"
        echo -e "     - Only requires: Homebrew Qt (qt@6)"
        echo -e "     - ${CYAN}Best for distribution${NC}"
        echo
        echo -e "  ${GREEN}3)${NC} Fully self-contained bundle ${YELLOW}(Experimental)${NC}"
        echo -e "     - Large size (~100-150MB)"
        echo -e "     - Embeds everything (Qt + poppler + SDL2)"
        echo -e "     - No dependencies required"
        echo -e "     - ${YELLOW}May have missing framework issues${NC}"
        echo
        echo -ne "${CYAN}Enter choice [1-3] (default: 2): ${NC}"
        read -r bundle_choice
        bundle_choice=${bundle_choice:-2}
        
        case $bundle_choice in
            1)
                echo -e "${GREEN}✓ Development bundle created${NC}"
                echo -e "${YELLOW}Requirements: poppler-qt6 in /opt/poppler-qt6, SDL2 in /usr/local/lib, Homebrew Qt${NC}"
                ;;
            2)
                echo -e "${CYAN}Creating portable bundle...${NC}"
                bundle_custom_libraries "SpeedyNote.app"
                echo -e "${GREEN}✓ Portable bundle created${NC}"
                echo -e "${YELLOW}Requirement: Homebrew Qt (qt@6)${NC}"
                ;;
            3)
                echo -e "${CYAN}Creating fully portable bundle...${NC}"
                # Bundle custom libraries first
                bundle_custom_libraries "SpeedyNote.app"
                
                # Then bundle Qt frameworks
                if command_exists macdeployqt; then
                    echo -e "${YELLOW}Bundling Qt frameworks...${NC}"
                    echo -e "${YELLOW}(This may take a minute...)${NC}"
                    /usr/local/opt/qt@6/bin/macdeployqt6 "SpeedyNote.app" 2>&1 | \
                        grep -v "ERROR: Cannot resolve rpath" | \
                        grep -v "using QList" || true
                    
                    # macdeployqt often misses QtDBus and other dependencies
                    # Manually copy missing Qt frameworks that are commonly needed
                    echo -e "${YELLOW}Copying additional Qt frameworks...${NC}"
                    local qt_path="/usr/local/opt/qt@6/lib"
                    local frameworks_dir="SpeedyNote.app/Contents/Frameworks"
                    
                    # List of additional frameworks that macdeployqt often misses
                    local additional_frameworks=("QtDBus")
                    
                    for fw in "${additional_frameworks[@]}"; do
                        if [[ -d "$qt_path/${fw}.framework" ]]; then
                            echo -e "${CYAN}  → Copying ${fw}.framework...${NC}"
                            cp -R "$qt_path/${fw}.framework" "$frameworks_dir/" 2>/dev/null || true
                            
                            # Fix the framework's install name
                            if [[ -f "$frameworks_dir/${fw}.framework/Versions/A/${fw}" ]]; then
                                install_name_tool -id "@executable_path/../Frameworks/${fw}.framework/Versions/A/${fw}" \
                                    "$frameworks_dir/${fw}.framework/Versions/A/${fw}" 2>/dev/null || true
                            fi
                        fi
                    done
                    
                    echo -e "${GREEN}✓ Qt frameworks bundled${NC}"
                else
                    echo -e "${RED}Error: macdeployqt not found${NC}"
                    echo -e "${YELLOW}Falling back to portable bundle (Qt not embedded)${NC}"
                fi
                echo -e "${GREEN}✓ Fully portable bundle created${NC}"
                echo -e "${CYAN}No external dependencies required!${NC}"
                ;;
            *)
                echo -e "${RED}Invalid choice, creating development bundle${NC}"
                ;;
        esac
        
        echo
        echo -e "${GREEN}✓ SpeedyNote.app bundle created${NC}"
        echo
        echo -e "${CYAN}Bundle location:${NC} ${YELLOW}$(pwd)/SpeedyNote.app${NC}"
        echo -e "${CYAN}Usage:${NC}"
        echo -e "  ${YELLOW}open SpeedyNote.app${NC}"
        echo -e "  ${YELLOW}./SpeedyNote.app/Contents/MacOS/NoteApp${NC}"
        echo
        
        # Show bundle size
        local bundle_size=$(du -sh "SpeedyNote.app" | awk '{print $1}')
        echo -e "${CYAN}Bundle size:${NC} ${YELLOW}${bundle_size}${NC}"
    fi
}

# Function to create DMG package for distribution
create_dmg_package() {
    echo
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo -e "${BLUE}   DMG Package Creation${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo
    
    if [[ ! -d "SpeedyNote.app" ]]; then
        echo -e "${RED}✗ SpeedyNote.app not found. Please create app bundle first.${NC}"
        return 1
    fi
    
    echo -e "${YELLOW}Would you like to create a distributable DMG package?${NC}"
    echo -e "  ${CYAN}1)${NC} Yes, create DMG with Qt@6 dependency installer"
    echo -e "  ${CYAN}2)${NC} Skip DMG creation"
    echo
    read -p "Enter your choice (1-2): " dmg_choice
    
    if [[ "$dmg_choice" != "1" ]]; then
        echo -e "${YELLOW}Skipping DMG creation${NC}"
        return 0
    fi
    
    echo
    echo -e "${CYAN}Creating DMG package...${NC}"
    
    # Clean up any previous DMG artifacts
    rm -rf dmg_temp SpeedyNote.dmg SpeedyNote_*.dmg
    
    # Create temporary directory for DMG contents
    mkdir -p dmg_temp
    
    # Copy the app bundle
    echo -e "${CYAN}  • Copying SpeedyNote.app...${NC}"
    cp -R SpeedyNote.app dmg_temp/
    
    # Create dependency installer script
    echo -e "${CYAN}  • Creating dependency installer...${NC}"
    cat > dmg_temp/Install_Dependencies.command << 'EOF'
#!/bin/bash

# SpeedyNote Dependency Installer
# This script installs required Qt6 libraries via Homebrew

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo -e "${BLUE}   SpeedyNote Dependency Installer${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
echo

# Check if Homebrew is installed
if ! command -v brew &> /dev/null; then
    echo -e "${YELLOW}Homebrew not found. Installing Homebrew...${NC}"
    echo
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Add Homebrew to PATH based on architecture
    if [[ $(uname -m) == "arm64" ]]; then
        echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/opt/homebrew/bin/brew shellenv)"
    else
        echo 'eval "$(/usr/local/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi

echo -e "${GREEN}✓ Homebrew is installed${NC}"
echo

# Check if Qt6 is installed
if brew list qt@6 &> /dev/null; then
    echo -e "${GREEN}✓ Qt@6 is already installed${NC}"
else
    echo -e "${CYAN}Installing Qt@6 (this may take a few minutes)...${NC}"
    brew install qt@6
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Qt@6 installed successfully${NC}"
    else
        echo -e "${RED}✗ Failed to install Qt@6${NC}"
        echo -e "${YELLOW}Please try running: brew install qt@6${NC}"
        read -p "Press Enter to exit..."
        exit 1
    fi
fi

echo
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo -e "${GREEN}   All dependencies installed!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
echo
echo -e "${CYAN}You can now run SpeedyNote by double-clicking the app.${NC}"
echo
read -p "Press Enter to exit..."
EOF
    
    chmod +x dmg_temp/Install_Dependencies.command
    
    # Create README
    echo -e "${CYAN}  • Creating README...${NC}"
    cat > dmg_temp/README.txt << 'EOF'
SpeedyNote for macOS
====================

Thank you for downloading SpeedyNote!

Installation Instructions:
--------------------------

1. First Time Setup:
   - Double-click "Install_Dependencies.command" to install Qt@6
   - Enter your password when prompted (for Homebrew installation)
   - Wait for the installation to complete

2. Run SpeedyNote:
   - Drag SpeedyNote.app to your Applications folder (optional)
   - Double-click SpeedyNote.app to launch

Note: If you see a security warning, go to:
System Settings > Privacy & Security > Allow apps from: App Store and identified developers

For more information, visit:
https://github.com/alpha-liu-01/SpeedyNote

Enjoy!
EOF
    
    # Create Applications symlink for easy drag-and-drop install
    echo -e "${CYAN}  • Creating Applications symlink...${NC}"
    ln -s /Applications dmg_temp/Applications
    
    # Get version from CMakeLists.txt or use default
    VERSION=$(grep "project(SpeedyNote VERSION" CMakeLists.txt | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')
    if [[ -z "$VERSION" ]]; then
        VERSION="0.12.1"
    fi
    
    DMG_NAME="SpeedyNote_v${VERSION}_macOS.dmg"
    
    # Create DMG
    echo -e "${CYAN}  • Building DMG image...${NC}"
    hdiutil create -volname "SpeedyNote" \
                   -srcfolder dmg_temp \
                   -ov \
                   -format UDZO \
                   -fs HFS+ \
                   "$DMG_NAME"
    
    if [[ $? -eq 0 ]]; then
        # Clean up
        rm -rf dmg_temp
        
        # Get DMG size
        DMG_SIZE=$(du -sh "$DMG_NAME" | awk '{print $1}')
        
        echo
        echo -e "${GREEN}✓ DMG package created successfully!${NC}"
        echo
        echo -e "${CYAN}Package location:${NC} ${YELLOW}$(pwd)/${DMG_NAME}${NC}"
        echo -e "${CYAN}Package size:${NC} ${YELLOW}${DMG_SIZE}${NC}"
        echo
        echo -e "${CYAN}Distribution instructions:${NC}"
        echo -e "  1. Share ${YELLOW}${DMG_NAME}${NC} with users"
        echo -e "  2. Users should run ${YELLOW}Install_Dependencies.command${NC} first"
        echo -e "  3. Users can then drag ${YELLOW}SpeedyNote.app${NC} to Applications"
        echo
    else
        echo -e "${RED}✗ Failed to create DMG${NC}"
        rm -rf dmg_temp
        return 1
    fi
}

# Main execution
main() {
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo -e "${BLUE}   SpeedyNote macOS Compilation Script${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo
    
    # Step 1: Verify environment
    check_project_directory
    
    # Step 2: Check Homebrew
    check_homebrew
    
    # Step 3: Check dependencies
    check_dependencies
    
    # Step 4: Set up Qt paths
    setup_qt_paths
    
    # Step 5: Build project
    build_project
    
    # Step 6: Optional app bundle
    create_app_bundle
    
    # Step 7: Optional DMG package creation
    if [[ -d "SpeedyNote.app" ]]; then
        create_dmg_package
    fi
    
    echo
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  SpeedyNote compilation completed successfully!${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    echo
    echo -e "${CYAN}To run SpeedyNote:${NC}"
    echo -e "  ${YELLOW}./build/NoteApp${NC}"
    if [[ -d "SpeedyNote.app" ]]; then
        echo -e "  ${YELLOW}or open SpeedyNote.app${NC}"
    fi
    echo
}

# Run main function
main "$@"

