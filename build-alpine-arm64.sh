#!/bin/bash
set -e

# SpeedyNote Alpine Linux ARM64 Packaging Script
# This script creates an Alpine Linux package using pre-built binaries from the build folder

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PKGNAME="speedynote"
PKGVER="1.0.2"
PKGREL="1"
MAINTAINER="SpeedyNote Team <speedynote@example.com>"
DESCRIPTION="A fast note-taking application with PDF annotation support and controller input"
URL="https://github.com/alpha-liu-01/SpeedyNote"
LICENSE="MIT"

echo -e "${BLUE}SpeedyNote Alpine Linux ARM64 Packaging Script${NC}"
echo "=============================================="
echo

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

# Function to check if build directory exists
check_build_directory() {
    if [[ ! -d "build" ]]; then
        echo -e "${RED}Error: Build directory not found${NC}"
        echo "Please compile SpeedyNote first using cmake and make"
        exit 1
    fi
    
    if [[ ! -f "build/NoteApp" ]]; then
        echo -e "${RED}Error: NoteApp executable not found in build directory${NC}"
        echo "Please compile SpeedyNote first using cmake and make"
        exit 1
    fi
    
    echo -e "${GREEN}Found pre-built NoteApp executable${NC}"
}

# Function to check packaging dependencies
check_packaging_dependencies() {
    echo -e "${YELLOW}Checking Alpine Linux packaging dependencies...${NC}"
    
    MISSING_DEPS=()
    
    if ! command_exists abuild; then
        MISSING_DEPS+=("abuild")
    fi
    if ! command_exists abuild-sign; then
        MISSING_DEPS+=("abuild")
    fi
    if ! command_exists tar; then
        MISSING_DEPS+=("tar")
    fi
    if ! command_exists sha256sum; then
        MISSING_DEPS+=("coreutils")
    fi
    
    if [[ ${#MISSING_DEPS[@]} -ne 0 ]]; then
        echo -e "${RED}Missing packaging dependencies:${NC}"
        for dep in "${MISSING_DEPS[@]}"; do
            echo "  - $dep"
        done
        echo
        echo -e "${YELLOW}Install with: sudo apk add ${MISSING_DEPS[*]}${NC}"
        return 1
    fi
    
    echo -e "${GREEN}All packaging dependencies are available!${NC}"
    return 0
}

# Function to get Alpine dependencies
get_dependencies() {
    echo "qt6-qtbase, poppler-qt6, sdl2, alsa-lib"
}

# Function to get Alpine build dependencies
get_build_dependencies() {
    echo "cmake, make, pkgconf, qt6-qtbase-dev, qt6-qttools-dev, poppler-qt6, poppler-qt5-dev, sdl2-dev, alsa-lib-dev"
}

# Function to create Alpine package
create_apk_package() {
    echo -e "${YELLOW}Creating Alpine Linux ARM64 package...${NC}"
    
    # Create Alpine package structure
    rm -rf alpine-pkg
    mkdir -p alpine-pkg
    cd alpine-pkg
    
    # Create source tarball first to calculate checksum
    echo -e "${YELLOW}Creating source tarball with pre-built binary...${NC}"
    
    # Go back to project root to access files
    cd ..
    
    # Create a minimal tarball with just the necessary files and pre-built binary
    mkdir -p alpine-pkg/speedynote-src
    cp -r resources/ alpine-pkg/speedynote-src/
    cp README.md alpine-pkg/speedynote-src/
    cp CMakeLists.txt alpine-pkg/speedynote-src/
    mkdir -p alpine-pkg/speedynote-src/prebuilt
    cp build/NoteApp alpine-pkg/speedynote-src/prebuilt/
    
    # Include Poppler font data for Asian text rendering
    if [ -d "share/" ]; then
        echo -e "${GREEN}Including Poppler font data for Asian text rendering...${NC}"
        cp -r share/ alpine-pkg/speedynote-src/
    else
        echo -e "${YELLOW}Warning: share/ folder not found - Asian text rendering may not work correctly${NC}"
    fi
    
    # Create tarball from alpine-pkg directory
    cd alpine-pkg
    tar -czf "${PKGNAME}-${PKGVER}.tar.gz" speedynote-src/
    rm -rf speedynote-src
    
    # Create APKBUILD first without checksum
    echo -e "${YELLOW}Creating APKBUILD...${NC}"
    cat > APKBUILD << EOF
# Maintainer: $MAINTAINER
pkgname=$PKGNAME
pkgver=$PKGVER
pkgrel=$PKGREL
pkgdesc="$DESCRIPTION"
url="$URL"
arch="aarch64"
license="MIT"
# depends="$(get_dependencies)"  # Commented out to avoid dependency issues
options="!check"  # No tests to run
source="\$pkgname-\$pkgver.tar.gz"
builddir="\$srcdir/speedynote-src"
install="\$pkgname.post-install"

build() {
    # Skip build entirely - using pre-built binaries
    return 0
}

package() {
    install -Dm755 "prebuilt/NoteApp" "\$pkgdir/usr/bin/speedynote"
    install -Dm644 "resources/icons/mainicon.png" "\$pkgdir/usr/share/pixmaps/speedynote.png"
    install -Dm644 README.md "\$pkgdir/usr/share/doc/\$pkgname/README.md"
    
    # Install translation files
    if [ -d "resources/translations" ]; then
        install -dm755 "\$pkgdir/usr/share/speedynote/translations"
        for qm_file in resources/translations/*.qm; do
            if [ -f "\$qm_file" ]; then
                install -m644 "\$qm_file" "\$pkgdir/usr/share/speedynote/translations/"
            fi
        done
    fi
    
    # Install Poppler font data for Asian text rendering
    if [ -d "share/poppler" ]; then
        install -dm755 "\$pkgdir/usr/share/poppler"
        cp -r share/poppler/* "\$pkgdir/usr/share/poppler/"
        find "\$pkgdir/usr/share/poppler" -type f -exec chmod 644 {} \\;
        find "\$pkgdir/usr/share/poppler" -type d -exec chmod 755 {} \\;
    fi
    
    # Create desktop file
    install -Dm644 /dev/stdin "\$pkgdir/usr/share/applications/speedynote.desktop" << EOFDESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=SpeedyNote
Comment=$DESCRIPTION
Exec=speedynote %F
Icon=speedynote
Terminal=false
StartupNotify=true
Categories=Office;Education;
Keywords=notes;pdf;annotation;writing;package;
MimeType=application/pdf;application/x-speedynote-package;
EOFDESKTOP

    # Create MIME type definition for .spn files
    install -Dm644 /dev/stdin "\$pkgdir/usr/share/mime/packages/application-x-speedynote-package.xml" << EOFMIME
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
    <mime-type type="application/x-speedynote-package">
        <comment>SpeedyNote Package</comment>
        <comment xml:lang="es">Paquete de SpeedyNote</comment>
        <comment xml:lang="fr">Package SpeedyNote</comment>
        <comment xml:lang="zh">SpeedyNote 包</comment>
        <icon name="speedynote"/>
        <glob pattern="*.spn"/>
        <magic priority="50">
            <match type="string" offset="0" value="Contents"/>
        </magic>
    </mime-type>
</mime-info>
EOFMIME
}
EOF
    
    # Generate checksums using abuild
    echo -e "${YELLOW}Generating checksums with abuild...${NC}"
    abuild checksum
    
    # Create post-install script
    echo -e "${YELLOW}Creating post-install script...${NC}"
    cat > "${PKGNAME}.post-install" << 'EOF'
#!/bin/sh

# Update desktop and MIME databases
update-desktop-database -q /usr/share/applications 2>/dev/null || true
update-mime-database /usr/share/mime 2>/dev/null || true

exit 0
EOF
    
    # Build package (source tarball already created above)
    echo -e "${YELLOW}Building Alpine package...${NC}"
    # Use -K to keep going on errors, -r for clean build, -d to skip dependency check
    abuild -K -r -d
    
    cd ..
    echo -e "${GREEN}Alpine package created successfully!${NC}"
    
    # Find and display the created package
    APK_FILE=$(find ~/packages -name "${PKGNAME}-${PKGVER}-*.apk" 2>/dev/null | head -1)
    if [[ -n "$APK_FILE" ]]; then
        echo -e "${GREEN}Package location: $APK_FILE${NC}"
        echo -e "${GREEN}Package size: $(du -h "$APK_FILE" | cut -f1)${NC}"
    else
        echo -e "${YELLOW}Package created in ~/packages/ directory${NC}"
    fi
}

# Function to clean up
cleanup() {
    echo -e "${YELLOW}Cleaning up build artifacts...${NC}"
    rm -rf alpine-pkg
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Function to show package information
show_package_info() {
    echo
    echo -e "${CYAN}=== Package Information ===${NC}"
    echo -e "Package name: ${PKGNAME}"
    echo -e "Version: ${PKGVER}-${PKGREL}"
    echo -e "Architecture: aarch64 (ARM64)"
    echo -e "Format: Alpine Linux (.apk)"
    echo -e "PDF file association: Enabled"
    echo -e ".spn file association: Enabled"
    echo
    
    echo -e "${CYAN}=== Installation Instructions ===${NC}"
    echo -e "1. Copy the .apk file to your ARM64 device"
    echo -e "2. Install with: sudo apk add --allow-untrusted /path/to/${PKGNAME}-${PKGVER}-${PKGREL}.apk"
    echo -e "3. Or add to a local repository and install normally"
    echo
    
    echo -e "${CYAN}=== Features ===${NC}"
    echo -e "✅ PDF Association: SpeedyNote available in 'Open with' menu for PDF files"
    echo -e "✅ .spn Package Association: Double-click .spn files to open in SpeedyNote"
    echo -e "✅ Desktop Integration: Application menu entry with proper categorization"
    echo -e "✅ MIME Type Support: Proper file type recognition"
    echo -e "✅ Translation Support: Multi-language interface support"
    echo -e "✅ Asian Text Support: Includes Poppler font data for Chinese, Japanese, Korean text rendering"
}

# Main execution
main() {
    echo -e "${BLUE}Starting Alpine Linux ARM64 packaging process...${NC}"
    
    # Step 1: Verify environment
    check_project_directory
    check_build_directory
    
    # Step 2: Check packaging dependencies
    if ! check_packaging_dependencies; then
        echo -e "${RED}Cannot continue without required dependencies.${NC}"
        echo -e "${YELLOW}Please install missing dependencies and try again.${NC}"
        exit 1
    fi
    
    # Step 3: Create package
    create_apk_package
    
    # Step 4: Cleanup
    cleanup
    
    # Step 5: Show final information
    show_package_info
    
    echo
    echo -e "${GREEN}Alpine Linux ARM64 packaging process completed successfully!${NC}"
    echo -e "${CYAN}Your weak ARM Chromebook tablet can now install SpeedyNote without compilation! 🚀${NC}"
}

# Run main function
main "$@"
