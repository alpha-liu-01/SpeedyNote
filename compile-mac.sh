#!/bin/bash
set -e

# SpeedyNote macOS Compilation and Packaging Script
# Supports both Intel (x86_64) and Apple Silicon (arm64)
# Uses MuPDF exclusively for PDF rendering and export

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m' # No Color

# Configuration
PKGNAME="SpeedyNote"
APP_BUNDLE="${PKGNAME}.app"
MIN_MACOS_VERSION="12.0"

# Command line options
PACKAGE_ONLY=false
FORCE_REBUILD=false
AUTO_DMG=false

# ============================================================================
# Usage and Argument Parsing
# ============================================================================

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "Options:"
    echo "  -p, --package-only   Skip build if executable exists, go straight to packaging"
    echo "  -f, --force          Force rebuild even if executable exists"
    echo "  -d, --dmg            Automatically create DMG without prompting"
    echo "  -h, --help           Show this help message"
    echo
    echo "Examples:"
    echo "  $0                   # Full build + package"
    echo "  $0 -p                # Package only (skip build if NoteApp exists)"
    echo "  $0 -p -d             # Package only + auto-create DMG"
    echo "  $0 -f                # Force full rebuild"
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--package-only)
                PACKAGE_ONLY=true
                shift
                ;;
            -f|--force)
                FORCE_REBUILD=true
                shift
                ;;
            -d|--dmg)
                AUTO_DMG=true
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                echo -e "${RED}Unknown option: $1${NC}"
                show_usage
                exit 1
                ;;
        esac
    done
}

# ============================================================================
# Helper Functions
# ============================================================================

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

check_project_directory() {
    if [[ ! -f "CMakeLists.txt" ]]; then
        echo -e "${RED}Error: This doesn't appear to be the SpeedyNote project directory${NC}"
        echo "Please run this script from the SpeedyNote project root directory"
        exit 1
    fi
}

detect_architecture() {
    local arch=$(uname -m)
    case $arch in
        x86_64|amd64)
            echo "x86_64"
            ;;
        arm64|aarch64)
            echo "arm64"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

get_homebrew_prefix() {
    local arch=$(detect_architecture)
    if [[ "$arch" == "arm64" ]]; then
        echo "/opt/homebrew"
    else
        echo "/usr/local"
    fi
}

# ============================================================================
# Dependency Management
# ============================================================================

check_homebrew() {
    echo -e "${YELLOW}Checking Homebrew installation...${NC}"
    
    if ! command_exists brew; then
        echo -e "${RED}Error: Homebrew is not installed${NC}"
        echo "Install with: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    echo -e "${GREEN}✓ Homebrew found${NC}"
}

check_and_install_dependencies() {
    echo -e "${YELLOW}Checking required dependencies...${NC}"
    
    local missing_deps=()
    
    # Required packages for MuPDF-only build
    local required_packages=(
        "qt@6"
        "mupdf"
        "cmake"
        "pkg-config"
    )
    
    for pkg in "${required_packages[@]}"; do
        if ! brew list "$pkg" &>/dev/null; then
            missing_deps+=("$pkg")
        fi
    done
    
    if [[ ${#missing_deps[@]} -eq 0 ]]; then
        echo -e "${GREEN}✓ All dependencies are installed${NC}"
    else
        echo -e "${YELLOW}Missing dependencies: ${missing_deps[*]}${NC}"
        echo -e "${CYAN}Installing missing dependencies...${NC}"
        brew install "${missing_deps[@]}"
        echo -e "${GREEN}✓ Dependencies installed${NC}"
    fi
}

setup_environment() {
    local prefix=$(get_homebrew_prefix)
    
    # Add Qt binaries to PATH for lrelease
    export PATH="${prefix}/opt/qt@6/bin:$PATH"
    export PKG_CONFIG_PATH="${prefix}/lib/pkgconfig:${prefix}/opt/qt@6/lib/pkgconfig:$PKG_CONFIG_PATH"
    
    echo -e "${CYAN}Using Homebrew prefix: ${prefix}${NC}"
}

# ============================================================================
# Build Functions
# ============================================================================

build_project() {
    echo -e "${YELLOW}Building SpeedyNote...${NC}"
    
    local arch=$(detect_architecture)
    echo -e "${CYAN}Detected architecture: ${arch}${NC}"
    
    case $arch in
        "arm64")
            echo -e "${MAGENTA}🍎 Optimization target: Apple Silicon (M1/M2/M3/M4)${NC}"
            ;;
        "x86_64")
            echo -e "${CYAN}🍎 Optimization target: Intel Mac (Nehalem+)${NC}"
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
        lrelease ./resources/translations/app_zh.ts \
                 ./resources/translations/app_fr.ts \
                 ./resources/translations/app_es.ts 2>/dev/null || true
        cp resources/translations/*.qm build/ 2>/dev/null || true
    fi
    
    cd build
    
    # Configure with CMake
    echo -e "${YELLOW}Configuring build...${NC}"
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_OSX_DEPLOYMENT_TARGET=${MIN_MACOS_VERSION} \
          ..
    
    # Build with parallel jobs
    local cpu_count=$(sysctl -n hw.ncpu)
    echo -e "${YELLOW}Compiling with ${cpu_count} parallel jobs...${NC}"
    make -j${cpu_count}
    
    if [[ ! -f "NoteApp" ]]; then
        echo -e "${RED}Build failed: NoteApp executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}✓ Build successful!${NC}"
}

# ============================================================================
# App Bundle Creation
# ============================================================================

get_version() {
    local version=$(grep "project(SpeedyNote VERSION" CMakeLists.txt | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')
    if [[ -z "$version" ]]; then
        version="1.0.0"
    fi
    echo "$version"
}

create_app_bundle() {
    echo -e "${YELLOW}Creating ${APP_BUNDLE}...${NC}"
    
    local version=$(get_version)
    
    # Clean up any existing bundle
    rm -rf "${APP_BUNDLE}"
    
    # Create app bundle structure
    mkdir -p "${APP_BUNDLE}/Contents/MacOS"
    mkdir -p "${APP_BUNDLE}/Contents/Resources"
    mkdir -p "${APP_BUNDLE}/Contents/Frameworks"
    
    # Copy executable
    cp build/NoteApp "${APP_BUNDLE}/Contents/MacOS/"
    
    # Create macOS icon (.icns)
    echo -e "${CYAN}  → Creating macOS icon...${NC}"
    if [[ -f "resources/icons/mainicon.png" ]]; then
        local iconset_dir="SpeedyNote.iconset"
        rm -rf "$iconset_dir"
        mkdir -p "$iconset_dir"
        
        # Generate all required icon sizes
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
        
        iconutil -c icns "$iconset_dir" -o "${APP_BUNDLE}/Contents/Resources/AppIcon.icns" 2>/dev/null || true
        rm -rf "$iconset_dir"
        
        if [[ -f "${APP_BUNDLE}/Contents/Resources/AppIcon.icns" ]]; then
            echo -e "${GREEN}    ✓ Icon created${NC}"
        else
            cp "resources/icons/mainicon.png" "${APP_BUNDLE}/Contents/Resources/"
        fi
    fi
    
    # Copy translation files
    if [[ -d "build" ]] && ls build/*.qm 1>/dev/null 2>&1; then
        mkdir -p "${APP_BUNDLE}/Contents/Resources/translations"
        cp build/*.qm "${APP_BUNDLE}/Contents/Resources/translations/" 2>/dev/null || true
        echo -e "${CYAN}  → Copied translation files${NC}"
    fi
    
    # Create Info.plist
    cat > "${APP_BUNDLE}/Contents/Info.plist" << EOF
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
    <string>${version}</string>
    <key>CFBundleShortVersionString</key>
    <string>${version}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>${MIN_MACOS_VERSION}</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>PDF Document</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>com.adobe.pdf</string>
            </array>
        </dict>
        <dict>
            <key>CFBundleTypeName</key>
            <string>SpeedyNote Bundle Export</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSHandlerRank</key>
            <string>Owner</string>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>snbx</string>
            </array>
        </dict>
    </array>
</dict>
</plist>
EOF
    
    echo -e "${GREEN}✓ App bundle structure created${NC}"
}

# ============================================================================
# Recursive Dependency Bundling
# ============================================================================

# Collect all non-system dependencies recursively
collect_dependencies() {
    local binary="$1"
    local deps_file="$2"
    local processed_file="$3"
    
    # Skip if already processed
    if grep -qFx "$binary" "$processed_file" 2>/dev/null; then
        return
    fi
    echo "$binary" >> "$processed_file"
    
    # Get direct dependencies using otool
    local deps=$(otool -L "$binary" 2>/dev/null | tail -n +2 | awk '{print $1}')
    
    for dep in $deps; do
        # Skip system libraries
        if [[ "$dep" == /System/* ]] || [[ "$dep" == /usr/lib/* ]]; then
            continue
        fi
        
        # Skip already-fixed paths
        if [[ "$dep" == "@executable_path"* ]] || [[ "$dep" == "@loader_path"* ]] || [[ "$dep" == "@rpath"* ]]; then
            continue
        fi
        
        # Only process Homebrew/Cellar libraries
        if [[ "$dep" == /usr/local/* ]] || [[ "$dep" == /opt/homebrew/* ]]; then
            # Check if file exists
            if [[ -f "$dep" ]]; then
                # Add to deps file if not already there
                if ! grep -qFx "$dep" "$deps_file" 2>/dev/null; then
                    echo "$dep" >> "$deps_file"
                    # Recursively process this dependency
                    collect_dependencies "$dep" "$deps_file" "$processed_file"
                fi
            fi
        fi
    done
}

bundle_dependencies() {
    local app_path="$1"
    local executable="${app_path}/Contents/MacOS/NoteApp"
    local frameworks_dir="${app_path}/Contents/Frameworks"
    
    echo -e "${YELLOW}Bundling dependencies recursively...${NC}"
    
    # Create temp files
    local deps_file=$(mktemp)
    local processed_file=$(mktemp)
    
    # Collect all dependencies recursively
    echo -e "${CYAN}  → Scanning dependencies...${NC}"
    collect_dependencies "$executable" "$deps_file" "$processed_file"
    
    # Also scan any libraries already in Frameworks (from macdeployqt)
    for lib in "${frameworks_dir}"/*.dylib "${frameworks_dir}"/*.framework/Versions/*/lib*.dylib 2>/dev/null; do
        if [[ -f "$lib" ]]; then
            collect_dependencies "$lib" "$deps_file" "$processed_file"
        fi
    done
    
    # Count and display
    local dep_count=$(wc -l < "$deps_file" | tr -d ' ')
    echo -e "${CYAN}  → Found ${dep_count} dependencies to bundle${NC}"
    
    # Copy each dependency
    local copied=0
    while IFS= read -r dep; do
        if [[ -n "$dep" ]] && [[ -f "$dep" ]]; then
            local libname=$(basename "$dep")
            
            # Skip if already exists
            if [[ ! -f "${frameworks_dir}/${libname}" ]]; then
                cp "$dep" "${frameworks_dir}/"
                copied=$((copied + 1))
                echo -e "${CYAN}    → ${libname}${NC}"
            fi
        fi
    done < "$deps_file"
    
    rm -f "$deps_file" "$processed_file"
    
    echo -e "${GREEN}  ✓ Copied ${copied} libraries${NC}"
    
    # Fix library paths
    echo -e "${YELLOW}Fixing library paths...${NC}"
    fix_library_paths "$app_path"
}

fix_library_paths() {
    local app_path="$1"
    local executable="${app_path}/Contents/MacOS/NoteApp"
    local frameworks_dir="${app_path}/Contents/Frameworks"
    
    # Get all original paths from the executable that need to be fixed
    echo -e "${CYAN}  → Analyzing library references...${NC}"
    
    # Fix executable - find all Homebrew references and fix them
    echo -e "${CYAN}  → Fixing executable...${NC}"
    
    # Get all non-system dependencies from executable
    local exe_deps=$(otool -L "$executable" 2>/dev/null | tail -n +2 | awk '{print $1}')
    
    for dep in $exe_deps; do
        # Skip system and already-fixed paths
        if [[ "$dep" == /System/* ]] || [[ "$dep" == /usr/lib/* ]] || [[ "$dep" == "@"* ]]; then
            continue
        fi
        
        local libname=$(basename "$dep")
        
        # Check if we have this library bundled
        if [[ -f "${frameworks_dir}/${libname}" ]]; then
            install_name_tool -change "$dep" \
                "@executable_path/../Frameworks/${libname}" "$executable" 2>/dev/null || true
        fi
    done
    
    # Fix each bundled library
    echo -e "${CYAN}  → Fixing bundled libraries...${NC}"
    for lib in "${frameworks_dir}"/*.dylib; do
        if [[ -f "$lib" ]]; then
            local libname=$(basename "$lib")
            
            # Set library's own ID
            install_name_tool -id "@executable_path/../Frameworks/${libname}" "$lib" 2>/dev/null || true
            
            # Get this library's dependencies
            local lib_deps=$(otool -L "$lib" 2>/dev/null | tail -n +2 | awk '{print $1}')
            
            for dep in $lib_deps; do
                # Skip system and already-fixed paths
                if [[ "$dep" == /System/* ]] || [[ "$dep" == /usr/lib/* ]] || [[ "$dep" == "@"* ]]; then
                    continue
                fi
                
                local dep_name=$(basename "$dep")
                
                # Check if we have this dependency bundled
                if [[ -f "${frameworks_dir}/${dep_name}" ]]; then
                    install_name_tool -change "$dep" \
                        "@executable_path/../Frameworks/${dep_name}" "$lib" 2>/dev/null || true
                fi
            done
        fi
    done
    
    # Remove all rpaths that point to Homebrew locations
    echo -e "${CYAN}  → Removing external rpaths...${NC}"
    local rpaths=$(otool -l "$executable" 2>/dev/null | grep -A2 LC_RPATH | grep "path " | awk '{print $2}')
    for rpath in $rpaths; do
        if [[ "$rpath" == /usr/local/* ]] || [[ "$rpath" == /opt/homebrew/* ]] || [[ "$rpath" == *Documents* ]]; then
            install_name_tool -delete_rpath "$rpath" "$executable" 2>/dev/null || true
        fi
    done
    
    echo -e "${GREEN}  ✓ Library paths fixed${NC}"
}

bundle_qt_frameworks() {
    local app_path="$1"
    local prefix=$(get_homebrew_prefix)
    local qt_path="${prefix}/opt/qt@6"
    
    echo -e "${YELLOW}Bundling Qt frameworks...${NC}"
    
    # Use macdeployqt if available
    local macdeployqt="${qt_path}/bin/macdeployqt"
    if [[ ! -f "$macdeployqt" ]]; then
        macdeployqt="${qt_path}/bin/macdeployqt6"
    fi
    
    if [[ -f "$macdeployqt" ]]; then
        echo -e "${CYAN}  → Running macdeployqt...${NC}"
        "$macdeployqt" "$app_path" -verbose=0 2>&1 | grep -v "ERROR: Cannot resolve rpath" || true
        echo -e "${GREEN}  ✓ Qt frameworks bundled${NC}"
    else
        echo -e "${YELLOW}  ⚠ macdeployqt not found, skipping Qt framework bundling${NC}"
        echo -e "${YELLOW}    App will require Qt@6 to be installed${NC}"
    fi
}

# ============================================================================
# DMG Creation
# ============================================================================

create_dmg() {
    local version=$(get_version)
    local arch=$(detect_architecture)
    local dmg_name="${PKGNAME}_v${version}_macOS_${arch}.dmg"
    
    echo -e "${YELLOW}Creating DMG: ${dmg_name}...${NC}"
    
    # Clean up previous DMG artifacts
    rm -rf dmg_temp "${dmg_name}"
    
    # Create temp directory for DMG contents
    mkdir -p dmg_temp
    
    # Copy the app bundle
    echo -e "${CYAN}  → Copying ${APP_BUNDLE}...${NC}"
    cp -R "${APP_BUNDLE}" dmg_temp/
    
    # Create Applications symlink
    ln -s /Applications dmg_temp/Applications
    
    # Create README
    cat > dmg_temp/README.txt << EOF
SpeedyNote for macOS (${arch})
==============================

Version: ${version}
Architecture: ${arch}

Installation:
1. Drag SpeedyNote.app to the Applications folder
2. Double-click to launch

Note: If you see a security warning on first launch:
- Go to System Settings > Privacy & Security
- Click "Open Anyway" for SpeedyNote

For more information:
https://github.com/alpha-liu-01/SpeedyNote
EOF
    
    # Create DMG
    echo -e "${CYAN}  → Building DMG image...${NC}"
    hdiutil create -volname "SpeedyNote" \
                   -srcfolder dmg_temp \
                   -ov \
                   -format UDZO \
                   -fs HFS+ \
                   "$dmg_name"
    
    # Clean up
    rm -rf dmg_temp
    
    if [[ -f "$dmg_name" ]]; then
        local dmg_size=$(du -sh "$dmg_name" | awk '{print $1}')
        echo -e "${GREEN}✓ DMG created: ${dmg_name} (${dmg_size})${NC}"
    else
        echo -e "${RED}✗ Failed to create DMG${NC}"
        return 1
    fi
}

# ============================================================================
# Verification
# ============================================================================

verify_bundle() {
    local app_path="$1"
    local executable="${app_path}/Contents/MacOS/NoteApp"
    local frameworks_dir="${app_path}/Contents/Frameworks"
    
    echo -e "${YELLOW}Verifying bundle...${NC}"
    
    local has_issues=false
    
    # Check executable for unresolved dependencies
    echo -e "${CYAN}  → Checking executable...${NC}"
    local exe_unresolved=$(otool -L "$executable" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -E "^(/opt/homebrew|/usr/local|/Users)" || true)
    
    if [[ -n "$exe_unresolved" ]]; then
        echo -e "${YELLOW}  ⚠ Executable has unbundled dependencies:${NC}"
        echo "$exe_unresolved" | while read -r dep; do
            echo -e "${RED}      $dep${NC}"
        done
        has_issues=true
    fi
    
    # Check each bundled library
    echo -e "${CYAN}  → Checking bundled libraries...${NC}"
    for lib in "${frameworks_dir}"/*.dylib; do
        if [[ -f "$lib" ]]; then
            local libname=$(basename "$lib")
            local lib_unresolved=$(otool -L "$lib" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -E "^(/opt/homebrew|/usr/local|/Users)" || true)
            
            if [[ -n "$lib_unresolved" ]]; then
                echo -e "${YELLOW}      ${libname} has unbundled deps:${NC}"
                echo "$lib_unresolved" | while read -r dep; do
                    echo -e "${RED}        $(basename $dep)${NC}"
                done
                has_issues=true
            fi
        fi
    done
    
    if [[ "$has_issues" == "false" ]]; then
        echo -e "${GREEN}  ✓ All dependencies appear to be properly bundled${NC}"
    else
        echo -e "${YELLOW}  ⚠ Some dependencies need attention (app may still work)${NC}"
    fi
    
    # Count bundled libraries
    local lib_count=$(ls -1 "${frameworks_dir}"/*.dylib 2>/dev/null | wc -l | tr -d ' ')
    local framework_count=$(ls -1d "${frameworks_dir}"/*.framework 2>/dev/null | wc -l | tr -d ' ')
    echo -e "${CYAN}  Bundled: ${lib_count} dylibs, ${framework_count} frameworks${NC}"
    
    # Show bundle size
    local bundle_size=$(du -sh "$app_path" | awk '{print $1}')
    echo -e "${CYAN}  Bundle size: ${bundle_size}${NC}"
}

# ============================================================================
# Main Execution
# ============================================================================

main() {
    # Parse command line arguments
    parse_arguments "$@"
    
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo -e "${BLUE}   SpeedyNote macOS Build Script${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════${NC}"
    echo
    
    local arch=$(detect_architecture)
    echo -e "${CYAN}Architecture: ${arch}${NC}"
    echo -e "${CYAN}PDF Provider: MuPDF (rendering + export)${NC}"
    echo
    
    # Step 1: Verify environment
    check_project_directory
    
    # Step 2: Check Homebrew
    check_homebrew
    
    # Step 3: Check and install dependencies
    check_and_install_dependencies
    
    # Step 4: Set up environment
    setup_environment
    
    # Step 5: Build project (or skip if executable exists and --package-only)
    local skip_build=false
    
    if [[ -f "build/NoteApp" ]] && [[ "$PACKAGE_ONLY" == "true" ]] && [[ "$FORCE_REBUILD" == "false" ]]; then
        echo -e "${GREEN}✓ Executable found: build/NoteApp${NC}"
        echo -e "${CYAN}  Skipping build (--package-only mode)${NC}"
        skip_build=true
    elif [[ -f "build/NoteApp" ]] && [[ "$PACKAGE_ONLY" == "false" ]] && [[ "$FORCE_REBUILD" == "false" ]]; then
        echo -e "${YELLOW}Executable already exists: build/NoteApp${NC}"
        echo -e "${CYAN}Would you like to rebuild? (y/n)${NC}"
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            echo -e "${CYAN}  Skipping build, using existing executable${NC}"
            skip_build=true
        fi
    fi
    
    if [[ "$skip_build" == "false" ]]; then
        build_project
    fi
    
    # Step 6: Create app bundle
    create_app_bundle
    
    # Step 7: Bundle Qt frameworks (using macdeployqt)
    bundle_qt_frameworks "${APP_BUNDLE}"
    
    # Step 8: Bundle additional dependencies (MuPDF and its deps)
    bundle_dependencies "${APP_BUNDLE}"
    
    # Step 9: Verify bundle
    verify_bundle "${APP_BUNDLE}"
    
    # Step 10: DMG creation
    echo
    if [[ "$AUTO_DMG" == "true" ]]; then
        create_dmg
    else
        echo -e "${CYAN}Would you like to create a distributable DMG? (y/n)${NC}"
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            create_dmg
        fi
    fi
    
    echo
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  Build completed successfully!${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════${NC}"
    echo
    echo -e "${CYAN}To run SpeedyNote:${NC}"
    echo -e "  ${YELLOW}open ${APP_BUNDLE}${NC}"
    echo -e "  ${YELLOW}or: ./${APP_BUNDLE}/Contents/MacOS/NoteApp${NC}"
    echo
}

# Run main function
main "$@"
