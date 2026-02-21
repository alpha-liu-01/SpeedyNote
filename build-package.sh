#!/bin/bash
set -e

# SpeedyNote Multi-Distribution Packaging Script
# This script automates the process of creating packages for multiple Linux distributions

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
PKGNAME="speedynote"
PKGVER="1.2.5"
PKGREL="1"
PKGARCH=$(uname -m)
MAINTAINER="SpeedyNote Team <info@speedynote.org>"
DESCRIPTION="A fast note-taking application with PDF annotation, PDF export, and controller input"
URL="https://github.com/alpha-liu-01/SpeedyNote"
LICENSE="GPL-3.0-or-later"

# Default values
PACKAGE_FORMATS=()
AUTO_DETECT=true

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  --deb, -deb       Create .deb package for Debian/Ubuntu"
    echo "  --rpm, -rpm       Create .rpm package for Red Hat/Fedora/SUSE"
    echo "  --arch, -arch     Create .pkg.tar.zst package for Arch Linux"
    echo "  --apk, -apk       Create .apk package for Alpine Linux"
    echo "  --all             Create packages for all supported distributions"
    echo "  --help, -h        Show this help message"
    echo
    echo "You can specify multiple formats: $0 --deb --rpm --arch"
    echo "If no option is specified, the script will auto-detect the distribution."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --deb|-deb)
            PACKAGE_FORMATS+=("deb")
            AUTO_DETECT=false
            shift
            ;;
        --rpm|-rpm)
            PACKAGE_FORMATS+=("rpm")
            AUTO_DETECT=false
            shift
            ;;
        --arch|-arch)
            PACKAGE_FORMATS+=("arch")
            AUTO_DETECT=false
            shift
            ;;
        --apk|-apk)
            PACKAGE_FORMATS+=("apk")
            AUTO_DETECT=false
            shift
            ;;
        --all)
            PACKAGE_FORMATS=("deb" "rpm" "arch" "apk")
            AUTO_DETECT=false
            shift
            ;;
        --help|-h)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

echo -e "${BLUE}SpeedyNote Multi-Distribution Packaging Script${NC}"
echo "=============================================="
echo

# Function to detect distribution
detect_distribution() {
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        case $ID in
            ubuntu|debian|linuxmint|pop)
                echo "deb"
                ;;
            fedora|rhel|centos|rocky|almalinux)
                echo "rpm"
                ;;
            opensuse*|sles)
                echo "rpm"
                ;;
            arch|manjaro|endeavouros|garuda)
                echo "arch"
                ;;
            alpine)
                echo "apk"
                ;;
            *)
                echo "unknown"
                ;;
        esac
    else
        echo "unknown"
    fi
}

# Auto-detect distribution if not specified
if [[ $AUTO_DETECT == true ]]; then
    DETECTED_DISTRO=$(detect_distribution)
    if [[ $DETECTED_DISTRO == "unknown" ]]; then
        echo -e "${RED}Unable to detect distribution. Please specify manually.${NC}"
        show_usage
        exit 1
    fi
    PACKAGE_FORMATS=("$DETECTED_DISTRO")
    echo -e "${YELLOW}Auto-detected distribution: $DETECTED_DISTRO${NC}"
else
    echo -e "${YELLOW}Target package formats: ${PACKAGE_FORMATS[*]}${NC}"
fi

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
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

# Function to check if we're in the right directory
check_project_directory() {
    if [[ ! -f "CMakeLists.txt" ]]; then
        echo -e "${RED}Error: This doesn't appear to be the SpeedyNote project directory${NC}"
        echo "Please run this script from the SpeedyNote project root directory"
        exit 1
    fi
}

# Function to get dependencies for each distribution
# Note: MuPDF linking strategy varies by distro:
#   - Ubuntu 24.04 (GitHub Actions): No libmupdf shared library available, uses static linking
#   - Debian 13+ (Trixie): Has libmupdf25.1, can use dynamic linking
#   - For local arm64 Debian builds with dynamic linking, manually add libmupdf25.1 to deps
# As of v1.2.1, SpeedyNote uses MuPDF exclusively (Poppler removed)
get_dependencies() {
    local format=$1
    case $format in
        deb)
            # No libmupdf dependency - Ubuntu uses static linking (no shared lib available)
            # For Debian 13+ arm64 builds with dynamic linking, add: libmupdf25.1
            echo "libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6"
            ;;
        rpm)
            # mupdf-libs provides libmupdf.so for dynamic linking
            echo "qt6-qtbase, mupdf-libs"
            ;;
        arch)
            # mupdf provides libmupdf.so
            echo "qt6-base, mupdf"
            ;;
        apk)
            # Space-separated (Alpine APKBUILD format, no commas)
            echo "qt6-qtbase qt6-qttools mupdf-libs"
            ;;
    esac
}

# Function to get build dependencies for each distribution
# As of v1.2.1, SpeedyNote uses MuPDF exclusively (Poppler removed)
get_build_dependencies() {
    local format=$1
    case $format in
        deb)
            # MuPDF for PDF rendering and export (static linking, needs dev packages for build)
            # jbig2dec, gumbo, mujs are MuPDF's optional dependencies that it was compiled with
            echo "cmake, make, pkg-config, qt6-base-dev, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, qt6-tools-dev, libmupdf-dev, libharfbuzz-dev, libfreetype-dev, libjpeg-dev, libopenjp2-7-dev, libjbig2dec0-dev, libgumbo-dev, libmujs-dev"
            ;;
        rpm)
            # MuPDF for PDF rendering and export (static linking, needs devel packages for build)
            # jbig2dec, gumbo-parser, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-qtbase-devel, qt6-qttools-devel, mupdf-devel, harfbuzz-devel, freetype-devel, libjpeg-turbo-devel, openjpeg2-devel, jbig2dec-devel, gumbo-parser-devel, mujs-devel"
            ;;
        arch)
            # MuPDF for PDF rendering and export (static linking)
            # jbig2dec, gumbo-parser, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-base, qt6-tools, mupdf, harfbuzz, freetype2, libjpeg-turbo, openjpeg2, jbig2dec, gumbo-parser, mujs"
            ;;
        apk)
            # MuPDF for PDF rendering and export (static linking, needs dev packages for build)
            # jbig2dec, gumbo, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-qtbase-dev, qt6-qttools-dev, mupdf-dev, harfbuzz-dev, freetype-dev, libjpeg-turbo-dev, openjpeg-dev, jbig2dec-dev, gumbo-dev, mujs-dev"
            ;;
    esac
}

# Function to check packaging dependencies
check_packaging_dependencies() {
    local format=$1
    echo -e "${YELLOW}Checking packaging dependencies for $format...${NC}"
    
    MISSING_DEPS=()
    
    case $format in
        deb)
            if ! command_exists dpkg-deb; then
                MISSING_DEPS+=("dpkg-dev")
            fi
            if ! command_exists debuild; then
                MISSING_DEPS+=("devscripts")
            fi
            ;;
        rpm)
            if ! command_exists rpmbuild; then
                MISSING_DEPS+=("rpm-build")
            fi
            if ! command_exists rpmspec; then
                MISSING_DEPS+=("rpm-devel")
            fi
            ;;
        arch)
            if ! command_exists makepkg; then
                MISSING_DEPS+=("base-devel")
            fi
            ;;
        apk)
            if ! command_exists abuild; then
                MISSING_DEPS+=("alpine-sdk")
            fi
            if ! command_exists abuild-sign; then
                MISSING_DEPS+=("abuild")
            fi
            ;;
    esac
    
    if [[ ${#MISSING_DEPS[@]} -ne 0 ]]; then
        echo -e "${RED}Missing packaging dependencies for $format:${NC}"
        for dep in "${MISSING_DEPS[@]}"; do
            echo "  - $dep"
        done
        echo
        case $format in
            deb)
                echo -e "${YELLOW}Install with: sudo apt-get install ${MISSING_DEPS[*]}${NC}"
                ;;
            rpm)
                echo -e "${YELLOW}Install with: sudo dnf install ${MISSING_DEPS[*]}${NC}"
                ;;
            arch)
                echo -e "${YELLOW}Install with: sudo pacman -S ${MISSING_DEPS[*]}${NC}"
                ;;
            apk)
                echo -e "${YELLOW}Install with: sudo apk add ${MISSING_DEPS[*]}${NC}"
                ;;
        esac
        return 1
    fi
    
    echo -e "${GREEN}All packaging dependencies are available for $format!${NC}"
    return 0
}

# Function to check and setup abuild signing keys (Alpine only)
check_abuild_keys() {
    echo -e "${YELLOW}Checking abuild signing key setup...${NC}"
    
    # Check if private key is configured
    if [[ ! -f "$HOME/.abuild/abuild.conf" ]] || ! grep -q "PACKAGER_PRIVKEY" "$HOME/.abuild/abuild.conf" 2>/dev/null; then
        echo -e "${YELLOW}No abuild signing key configured. Generating one...${NC}"
        abuild-keygen -a -n
    fi
    
    # Get the private key path from config
    PRIVKEY=$(grep "PACKAGER_PRIVKEY" "$HOME/.abuild/abuild.conf" 2>/dev/null | cut -d'"' -f2)
    if [[ -z "$PRIVKEY" ]] || [[ ! -f "$PRIVKEY" ]]; then
        echo -e "${RED}Error: Could not find private key${NC}"
        echo "Please run: abuild-keygen -a"
        return 1
    fi
    
    # Check if the corresponding public key is installed in /etc/apk/keys
    PUBKEY="${PRIVKEY}.pub"
    PUBKEY_NAME=$(basename "$PUBKEY")
    
    if [[ ! -f "/etc/apk/keys/$PUBKEY_NAME" ]]; then
        echo -e "${YELLOW}Public key not installed in /etc/apk/keys/${NC}"
        echo -e "${CYAN}Attempting to install public key (requires sudo)...${NC}"
        if sudo cp "$PUBKEY" "/etc/apk/keys/"; then
            echo -e "${GREEN}Public key installed successfully!${NC}"
        else
            echo -e "${RED}Could not install public key automatically.${NC}"
            echo -e "${YELLOW}Please run manually: sudo cp $PUBKEY /etc/apk/keys/${NC}"
            echo -e "${YELLOW}Continuing anyway - package will be built but index may fail...${NC}"
        fi
    else
        echo -e "${GREEN}Signing key is properly configured!${NC}"
    fi
    
    return 0
}

# Function to build the project
build_project() {
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
    
    # Clean and create build directory
    rm -rf build
    mkdir -p build
    
    # Copy pre-compiled translation files if they exist
    if [ -d "resources/translations" ] && ls resources/translations/*.qm 1>/dev/null 2>&1; then
        echo -e "${YELLOW}Copying translation files...${NC}"
        cp resources/translations/*.qm build/ 2>/dev/null || true
    fi
    
    cd build
    
    # Configure and build with optimizations
    echo -e "${YELLOW}Configuring build with maximum performance optimizations...${NC}"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
    
    # Determine number of parallel jobs based on architecture
    # ARM64 devices often have limited memory/thermal headroom, so use half the cores
    ARCH=$(uname -m)
    CORES=$(nproc)
    echo -e "${YELLOW}Detected architecture: $ARCH with $CORES cores${NC}"
    
    if [[ "$ARCH" == "aarch64" ]] || [[ "$ARCH" == "arm64" ]]; then
        JOBS=$(( (CORES + 1) / 2 ))
        if [[ $JOBS -lt 1 ]]; then JOBS=1; fi
        echo -e "${YELLOW}Compiling with $JOBS parallel jobs (ARM64: half of $CORES cores)...${NC}"
    else
        JOBS=$CORES
        echo -e "${YELLOW}Compiling with $JOBS parallel jobs (x64: all $CORES cores)...${NC}"
    fi
    
    make -j"$JOBS"
    
    if [[ ! -f "speedynote" ]]; then
        echo -e "${RED}Build failed: speedynote executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}Build successful!${NC}"
}

# Function to create DEB package
create_deb_package() {
    echo -e "${YELLOW}Creating DEB package...${NC}"
    
    PKG_DIR="debian-pkg"
    rm -rf "$PKG_DIR"
    mkdir -p "$PKG_DIR/DEBIAN"
    mkdir -p "$PKG_DIR/usr/bin"
    mkdir -p "$PKG_DIR/usr/share/applications"
    mkdir -p "$PKG_DIR/usr/share/pixmaps"
    mkdir -p "$PKG_DIR/usr/share/doc/$PKGNAME"
    
    # Create control file
    cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: $PKGNAME
Version: $PKGVER-$PKGREL
Architecture: $(dpkg --print-architecture)
Maintainer: $MAINTAINER
Depends: $(get_dependencies deb)
Section: editors
Priority: optional
Homepage: $URL
Description: $DESCRIPTION
 SpeedyNote is a fast and efficient note-taking application with PDF annotation,
 PDF export support, and controller input capabilities.
EOF
    
    # Create postinst script for desktop database update
    cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Update desktop database
if [ -x /usr/bin/update-desktop-database ]; then
    update-desktop-database -q /usr/share/applications
fi

exit 0
EOF
    
    # Create postrm script for cleanup
    cat > "$PKG_DIR/DEBIAN/postrm" << 'EOF'
#!/bin/bash
set -e

if [ "$1" = "remove" ]; then
    # Update desktop database
    if [ -x /usr/bin/update-desktop-database ]; then
        update-desktop-database -q /usr/share/applications
    fi
fi

exit 0
EOF
    
    chmod 755 "$PKG_DIR/DEBIAN/postinst"
    chmod 755 "$PKG_DIR/DEBIAN/postrm"
    
    # Install files
    cp build/speedynote "$PKG_DIR/usr/bin/speedynote"
    cp README.md "$PKG_DIR/usr/share/doc/$PKGNAME/"
    
    # Install icons (name must match Icon= in .desktop file: org.speedynote.SpeedyNote)
    mkdir -p "$PKG_DIR/usr/share/icons/hicolor/scalable/apps"
    cp resources/icons/mainicon.svg "$PKG_DIR/usr/share/icons/hicolor/scalable/apps/org.speedynote.SpeedyNote.svg"
    cp resources/icons/mainicon.svg "$PKG_DIR/usr/share/pixmaps/org.speedynote.SpeedyNote.svg"
    
    # Install translation files
    mkdir -p "$PKG_DIR/usr/share/speedynote/translations"
    if [ -d "resources/translations" ]; then
        cp resources/translations/*.qm "$PKG_DIR/usr/share/speedynote/translations/" 2>/dev/null || true
    fi
    
    # Install desktop file from committed source
    cp data/org.speedynote.SpeedyNote.desktop "$PKG_DIR/usr/share/applications/org.speedynote.SpeedyNote.desktop"
    
    # Build package
    dpkg-deb --build "$PKG_DIR" "${PKGNAME}_${PKGVER}-${PKGREL}_$(dpkg --print-architecture).deb"

    echo -e "${GREEN}DEB package created: ${PKGNAME}_${PKGVER}-${PKGREL}_$(dpkg --print-architecture).deb${NC}"
}

# Function to create RPM package
create_rpm_package() {
    echo -e "${YELLOW}Creating RPM package...${NC}"
    
    # Setup RPM build environment
    mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    
    # Create source tarball with proper directory structure
    CURRENT_DIR=$(basename "$PWD")
    cd ..
    tar -czf ~/rpmbuild/SOURCES/${PKGNAME}-${PKGVER}.tar.gz \
        --exclude=build \
        --exclude=.git* \
        --exclude="*.rpm" \
        --exclude="*.deb" \
        --exclude="*.pkg.tar.zst" \
        --exclude="*.apk" \
        --transform "s|^${CURRENT_DIR}|${PKGNAME}-${PKGVER}|" \
        "${CURRENT_DIR}/"
    cd "${CURRENT_DIR}"
    
    # Create spec file
    cat > ~/rpmbuild/SPECS/${PKGNAME}.spec << EOF
Name:           $PKGNAME
Version:        $PKGVER
Release:        $PKGREL%{?dist}
Summary:        $DESCRIPTION
License:        $LICENSE
URL:            $URL
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  $(get_build_dependencies rpm)
Requires:       $(get_dependencies rpm)

%description
SpeedyNote is a fast and efficient note-taking application with PDF annotation,
PDF export support, and controller input capabilities.

%prep
%setup -q

%build
%cmake -DCMAKE_BUILD_TYPE=Release
# ARM64 devices often have limited memory/thermal headroom, so use half the cores
%ifarch aarch64
%cmake_build -- -j\$(( (\$(nproc) + 1) / 2 ))
%else
%cmake_build
%endif

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/share/pixmaps
mkdir -p %{buildroot}/usr/share/icons/hicolor/scalable/apps
mkdir -p %{buildroot}/usr/share/doc/%{name}

install -m755 %{_vpath_builddir}/speedynote %{buildroot}/usr/bin/speedynote
install -m644 resources/icons/mainicon.svg %{buildroot}/usr/share/icons/hicolor/scalable/apps/org.speedynote.SpeedyNote.svg
install -m644 resources/icons/mainicon.svg %{buildroot}/usr/share/pixmaps/org.speedynote.SpeedyNote.svg
install -m644 README.md %{buildroot}/usr/share/doc/%{name}/

# Install translation files
mkdir -p %{buildroot}/usr/share/speedynote/translations
if [ -d "resources/translations" ]; then
    cp resources/translations/*.qm %{buildroot}/usr/share/speedynote/translations/ 2>/dev/null || true
fi

# Install committed desktop file
install -Dm644 data/org.speedynote.SpeedyNote.desktop %{buildroot}/usr/share/applications/org.speedynote.SpeedyNote.desktop

%post
/usr/bin/update-desktop-database -q /usr/share/applications || :

%postun
/usr/bin/update-desktop-database -q /usr/share/applications || :

%files
/usr/bin/speedynote
/usr/share/applications/org.speedynote.SpeedyNote.desktop
/usr/share/icons/hicolor/scalable/apps/org.speedynote.SpeedyNote.svg
/usr/share/pixmaps/org.speedynote.SpeedyNote.svg
/usr/share/doc/%{name}/README.md
/usr/share/speedynote/translations/

%changelog
* $(date '+%a %b %d %Y') $MAINTAINER - $PKGVER-$PKGREL
- Initial package with PDF file association support
EOF
    
    # Build RPM
    rpmbuild -ba ~/rpmbuild/SPECS/${PKGNAME}.spec
    
    # Copy to current directory
    cp ~/rpmbuild/RPMS/${PKGARCH}/${PKGNAME}-${PKGVER}-${PKGREL}.*.rpm .
    
    echo -e "${GREEN}RPM package created: ${PKGNAME}-${PKGVER}-${PKGREL}.*.rpm${NC}"
}

# Function to create Arch package
create_arch_package() {
    echo -e "${YELLOW}Creating Arch package...${NC}"
    
    # Create a dedicated build directory for makepkg
    ARCH_BUILD_DIR="arch-pkg"
    rm -rf "$ARCH_BUILD_DIR"
    mkdir -p "$ARCH_BUILD_DIR"
    
    # Create source tarball with proper directory structure (like RPM does)
    # The tarball root should be ${PKGNAME}-${PKGVER}/ not ./
    CURRENT_DIR=$(basename "$PWD")
    cd ..
    tar -czf "${CURRENT_DIR}/${ARCH_BUILD_DIR}/${PKGNAME}-${PKGVER}.tar.gz" \
        --exclude=build \
        --exclude=.git* \
        --exclude="*.tar.gz" \
        --exclude="*.pkg.tar.zst" \
        --exclude="*.rpm" \
        --exclude="*.deb" \
        --exclude="*.apk" \
        --exclude=pkg \
        --exclude=src \
        --exclude=arch-pkg \
        --exclude=debian-pkg \
        --exclude=alpine-pkg \
        --transform "s|^${CURRENT_DIR}|${PKGNAME}-${PKGVER}|" \
        "${CURRENT_DIR}/"
    cd "${CURRENT_DIR}"
    
    # Create PKGBUILD in the build directory
    cat > "$ARCH_BUILD_DIR/PKGBUILD" << EOF
# Maintainer: $MAINTAINER
pkgname=$PKGNAME
pkgver=$PKGVER
pkgrel=$PKGREL
pkgdesc="$DESCRIPTION"
arch=("$PKGARCH")
url="$URL"
license=('GPL-3.0-or-later')
depends=($(get_dependencies arch | tr ',' ' '))
makedepends=($(get_build_dependencies arch | tr ',' ' '))
source=("\${pkgname}-\${pkgver}.tar.gz")
sha256sums=('SKIP')

build() {
    cd "\$srcdir/\${pkgname}-\${pkgver}"
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # Limit parallelism to avoid OOM on systems with many cores but limited RAM
    # Use half of available cores, minimum 1
    local jobs=\$(( (\$(nproc) + 1) / 2 ))
    cmake --build build --parallel \$jobs
}

package() {
    cd "\$srcdir/\${pkgname}-\${pkgver}"
    install -Dm755 "build/speedynote" "\$pkgdir/usr/bin/speedynote"
    install -Dm644 "resources/icons/mainicon.svg" "\$pkgdir/usr/share/icons/hicolor/scalable/apps/org.speedynote.SpeedyNote.svg"
    install -Dm644 "resources/icons/mainicon.svg" "\$pkgdir/usr/share/pixmaps/org.speedynote.SpeedyNote.svg"
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
    
    # Install committed desktop file
    install -Dm644 "data/org.speedynote.SpeedyNote.desktop" "\$pkgdir/usr/share/applications/org.speedynote.SpeedyNote.desktop"
}

post_install() {
    update-desktop-database -q
}

post_upgrade() {
    update-desktop-database -q
}

post_remove() {
    update-desktop-database -q
}
EOF
    
    # Build package from the dedicated directory
    cd "$ARCH_BUILD_DIR"
    makepkg -f
    
    # Copy the package back to project root
    cd ..
    cp "$ARCH_BUILD_DIR/${PKGNAME}-${PKGVER}-${PKGREL}-${PKGARCH}.pkg.tar.zst" . 2>/dev/null || \
    cp "$ARCH_BUILD_DIR"/${PKGNAME}-${PKGVER}-${PKGREL}-*.pkg.tar.zst . 2>/dev/null || true
    
    echo -e "${GREEN}Arch package created: ${PKGNAME}-${PKGVER}-${PKGREL}-${PKGARCH}.pkg.tar.zst${NC}"
}

# Function to create Alpine package
# Uses pre-built binary (proven approach from build-alpine-arm64.sh)
create_apk_package() {
    echo -e "${YELLOW}Creating Alpine package...${NC}"
    
    # Detect Alpine architecture (same as uname -m: x86_64, aarch64, etc.)
    local apk_arch=$(uname -m)
    echo -e "${CYAN}Target architecture: ${apk_arch}${NC}"
    
    # Verify pre-built binary exists
    if [[ ! -f "build/speedynote" ]]; then
        echo -e "${RED}Error: speedynote executable not found in build directory${NC}"
        echo "Please compile SpeedyNote first (build step should have run)"
        exit 1
    fi
    
    # Clean and create Alpine package structure
    rm -rf alpine-pkg
    mkdir -p alpine-pkg/speedynote-src/prebuilt
    
    # Create source tarball with pre-built binary and needed resources
    cp -r resources/ alpine-pkg/speedynote-src/
    cp -r data/ alpine-pkg/speedynote-src/
    cp README.md alpine-pkg/speedynote-src/
    cp CMakeLists.txt alpine-pkg/speedynote-src/
    cp build/speedynote alpine-pkg/speedynote-src/prebuilt/
    
    cd alpine-pkg
    tar -czf "${PKGNAME}-${PKGVER}.tar.gz" speedynote-src/
    rm -rf speedynote-src
    
    # Create APKBUILD (pre-built binary — skip build step)
    echo -e "${YELLOW}Creating APKBUILD...${NC}"
    cat > APKBUILD << EOF
# Maintainer: $MAINTAINER
pkgname=$PKGNAME
pkgver=$PKGVER
pkgrel=$PKGREL
pkgdesc="$DESCRIPTION"
url="$URL"
arch="$apk_arch"
license="GPL-3.0-or-later"
depends="$(get_dependencies apk)"
options="!check"
source="\$pkgname-\$pkgver.tar.gz"
builddir="\$srcdir/speedynote-src"
install="\$pkgname.post-install"

build() {
    # Skip build — using pre-built binary
    return 0
}

package() {
    install -Dm755 "prebuilt/speedynote" "\$pkgdir/usr/bin/speedynote"
    install -Dm644 "resources/icons/mainicon.svg" "\$pkgdir/usr/share/icons/hicolor/scalable/apps/org.speedynote.SpeedyNote.svg"
    install -Dm644 "resources/icons/mainicon.svg" "\$pkgdir/usr/share/pixmaps/org.speedynote.SpeedyNote.svg"
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
    
    # Install committed desktop file
    install -Dm644 "data/org.speedynote.SpeedyNote.desktop" "\$pkgdir/usr/share/applications/org.speedynote.SpeedyNote.desktop"
}
EOF
    
    # Generate checksums using abuild
    echo -e "${YELLOW}Generating checksums with abuild...${NC}"
    abuild checksum
    
    # Create post-install script
    cat > "${PKGNAME}.post-install" << 'EOF'
#!/bin/sh

# Update desktop and MIME databases
update-desktop-database -q /usr/share/applications 2>/dev/null || true
update-mime-database /usr/share/mime 2>/dev/null || true

exit 0
EOF
    
    # Build package
    # -K: keep going on errors, -r: clean build, -d: skip dependency check
    echo -e "${YELLOW}Building Alpine package...${NC}"
    set +e
    abuild -K -r -d 2>&1 | tee /tmp/abuild_output.log
    ABUILD_RESULT=${PIPESTATUS[0]}
    set -e
    
    cd ..
    
    # Find the created package
    APK_FILE=$(find ~/packages -name "${PKGNAME}-${PKGVER}-*.apk" -newer alpine-pkg/APKBUILD 2>/dev/null | head -1)
    
    if [[ -n "$APK_FILE" ]] && [[ -f "$APK_FILE" ]]; then
        echo -e "${GREEN}Alpine package created successfully!${NC}"
        echo -e "${GREEN}Package location: $APK_FILE${NC}"
        echo -e "${GREEN}Package size: $(du -h "$APK_FILE" | cut -f1)${NC}"
        
        # Warn about UNTRUSTED signature if index creation failed
        if [[ $ABUILD_RESULT -ne 0 ]]; then
            if grep -q "UNTRUSTED signature" /tmp/abuild_output.log 2>/dev/null; then
                echo
                echo -e "${YELLOW}Note: Repository index creation failed due to untrusted signature.${NC}"
                echo -e "${YELLOW}The .apk package itself was created successfully!${NC}"
                echo -e "${YELLOW}To fix this for future builds, run:${NC}"
                echo -e "${CYAN}  sudo cp ~/.abuild/*.rsa.pub /etc/apk/keys/${NC}"
            fi
        fi
    else
        echo -e "${RED}Error: Package creation failed${NC}"
        echo -e "${YELLOW}Check the build log above for details${NC}"
        exit 1
    fi
    
    rm -f /tmp/abuild_output.log
}

# Function to clean up
cleanup() {
    echo -e "${YELLOW}Cleaning up build artifacts...${NC}"
    rm -rf build debian-pkg alpine-pkg arch-pkg
    rm -f "${PKGNAME}-${PKGVER}.tar.gz"
    rm -f PKGBUILD  # Remove any stale PKGBUILD from project root
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Function to show package information
show_package_info() {
    echo
    echo -e "${CYAN}=== Package Information ===${NC}"
    echo -e "Package name: ${PKGNAME}"
    echo -e "Version: ${PKGVER}-${PKGREL}"
    echo -e "Formats created: ${PACKAGE_FORMATS[*]}"
    echo -e "PDF file association: Enabled"
    echo
    
    echo -e "${CYAN}=== Created Packages ===${NC}"
    for format in "${PACKAGE_FORMATS[@]}"; do
        case $format in
            deb)
                if [[ -f "${PKGNAME}_${PKGVER}-${PKGREL}_$(dpkg --print-architecture).deb" ]]; then
                    echo -e "DEB: ${PKGNAME}_${PKGVER}-${PKGREL}_$(dpkg --print-architecture).deb ($(du -h "${PKGNAME}_${PKGVER}-${PKGREL}_$(dpkg --print-architecture).deb" | cut -f1))"
                fi
                ;;
            rpm)
                RPM_FILE=$(ls ${PKGNAME}-${PKGVER}-${PKGREL}.*.rpm 2>/dev/null | head -1)
                if [[ -n "$RPM_FILE" ]]; then
                    echo -e "RPM: $RPM_FILE ($(du -h "$RPM_FILE" | cut -f1))"
                fi
                ;;
            arch)
                if [[ -f "${PKGNAME}-${PKGVER}-${PKGREL}-${PKGARCH}.pkg.tar.zst" ]]; then
                    echo -e "Arch: ${PKGNAME}-${PKGVER}-${PKGREL}-${PKGARCH}.pkg.tar.zst ($(du -h "${PKGNAME}-${PKGVER}-${PKGREL}-${PKGARCH}.pkg.tar.zst" | cut -f1))"
                fi
                ;;
            apk)
                APK_PKG=$(find ~/packages -name "${PKGNAME}-${PKGVER}-*.apk" 2>/dev/null | head -1)
                if [[ -n "$APK_PKG" ]] && [[ -f "$APK_PKG" ]]; then
                    echo -e "Alpine: $APK_PKG ($(du -h "$APK_PKG" | cut -f1))"
                    echo -e "  Install with: sudo apk add --allow-untrusted $APK_PKG"
                else
                    echo -e "Alpine: Check ~/packages/ for .apk file"
                fi
                ;;
        esac
    done
    
    echo
    echo -e "${CYAN}=== File Association ===${NC}"
    echo -e "✅ PDF Association: SpeedyNote available in 'Open with' menu for PDF files"
    echo -e "✅ Launcher Integration: Use SpeedyNote launcher for creating and importing notebooks"
}

# Main execution
main() {
    echo -e "${BLUE}Starting multi-distribution packaging process...${NC}"
    
    # Step 1: Verify environment
    check_project_directory
    
    # Step 2: Check packaging dependencies for each format
    FAILED_FORMATS=()
    for format in "${PACKAGE_FORMATS[@]}"; do
        if ! check_packaging_dependencies "$format"; then
            FAILED_FORMATS+=("$format")
        fi
    done
    
    if [[ ${#FAILED_FORMATS[@]} -gt 0 ]]; then
        echo -e "${RED}Cannot continue with formats: ${FAILED_FORMATS[*]}${NC}"
        echo -e "${YELLOW}Please install missing dependencies and try again.${NC}"
        exit 1
    fi
    
    # Step 2b: Check abuild signing keys (Alpine only)
    if [[ " ${PACKAGE_FORMATS[*]} " =~ " apk " ]]; then
        check_abuild_keys
    fi
    
    # Step 3: Build project (needed for DEB and APK which use pre-built binary)
    # RPM and Arch build from source in their respective build systems
    if [[ " ${PACKAGE_FORMATS[*]} " =~ " deb " ]] || [[ " ${PACKAGE_FORMATS[*]} " =~ " apk " ]]; then
        build_project
    else
        echo -e "${YELLOW}Skipping pre-build (target formats build from source)${NC}"
    fi
    
    # Step 4: Create packages
    for format in "${PACKAGE_FORMATS[@]}"; do
        case $format in
            deb)
                create_deb_package
                ;;
            rpm)
                create_rpm_package
                ;;
            arch)
                create_arch_package
                ;;
            apk)
                create_apk_package
                ;;
        esac
    done
    
    # Step 5: Cleanup
    cleanup
    
    # Step 6: Show final information
    show_package_info
    
    echo
    echo -e "${GREEN}Multi-distribution packaging process completed successfully!${NC}"
}

# Run main function
main "$@" 
