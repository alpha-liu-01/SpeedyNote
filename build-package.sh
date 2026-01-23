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
PKGVER="1.1.0"
PKGREL="1"
PKGARCH=$(uname -m)
MAINTAINER="SpeedyNote Team"
DESCRIPTION="A fast note-taking application with PDF annotation, PDF export, and controller input"
URL="https://github.com/alpha-liu-01/SpeedyNote"
LICENSE="MIT"

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
get_dependencies() {
    local format=$1
    case $format in
        deb)
            echo "libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, libpoppler-qt6-3t64 | libpoppler-qt6-3, libsdl2-2.0-0, libasound2"
            ;;
        rpm)
            echo "qt6-qtbase, poppler-qt6, SDL2, alsa-lib"
            ;;
        arch)
            echo "qt6-base, poppler-qt6, sdl2-compat, alsa-lib"
            ;;
        apk)
            echo "qt6-qtbase, poppler-qt6, sdl2, alsa-lib"
            ;;
    esac
}

# Function to get build dependencies for each distribution
get_build_dependencies() {
    local format=$1
    case $format in
        deb)
            # MuPDF for PDF export (static linking, needs dev packages for build)
            # jbig2dec, gumbo, mujs are MuPDF's optional dependencies that it was compiled with
            echo "cmake, make, pkg-config, qt6-base-dev, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, qt6-tools-dev, libpoppler-qt6-dev, libsdl2-dev, libasound2-dev, libmupdf-dev, libharfbuzz-dev, libfreetype-dev, libjpeg-dev, libopenjp2-7-dev, libjbig2dec0-dev, libgumbo-dev, libmujs-dev"
            ;;
        rpm)
            # MuPDF for PDF export (static linking, needs devel packages for build)
            # jbig2dec, gumbo-parser, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-qtbase-devel, qt6-qttools-devel, poppler-qt6-devel, SDL2-devel, alsa-lib-devel, mupdf-devel, harfbuzz-devel, freetype-devel, libjpeg-turbo-devel, openjpeg2-devel, jbig2dec-devel, gumbo-parser-devel, mujs-devel"
            ;;
        arch)
            # MuPDF for PDF export (static linking)
            # jbig2dec, gumbo-parser, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-base, qt6-tools, poppler-qt6, sdl2-compat, alsa-lib, mupdf, harfbuzz, freetype2, libjpeg-turbo, openjpeg2, jbig2dec, gumbo-parser, mujs"
            ;;
        apk)
            # MuPDF for PDF export (static linking, needs dev packages for build)
            # jbig2dec, gumbo, mujs are MuPDF's optional dependencies
            echo "cmake, make, pkgconf, qt6-qtbase-dev, qt6-qttools-dev, poppler-qt6, poppler-qt5-dev, sdl2-dev, alsa-lib-dev, mupdf-dev, harfbuzz-dev, freetype-dev, libjpeg-turbo-dev, openjpeg-dev, jbig2dec-dev, gumbo-dev, mujs-dev"
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
    if [[ "$(uname -m)" == "aarch64" ]]; then
        JOBS=$(( ($(nproc) + 1) / 2 ))
        echo -e "${YELLOW}Compiling with $JOBS parallel jobs (ARM64: half of $(nproc) cores)...${NC}"
    else
        JOBS=$(nproc)
        echo -e "${YELLOW}Compiling with $JOBS parallel jobs (x64: all cores)...${NC}"
    fi
    make -j$JOBS
    
    if [[ ! -f "NoteApp" ]]; then
        echo -e "${RED}Build failed: NoteApp executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}Build successful!${NC}"
}

# Function to create desktop file with PDF MIME type association
create_desktop_file() {
    local desktop_file="$1"
    cat > "$desktop_file" << EOF
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
Keywords=notes;pdf;annotation;writing;
MimeType=application/pdf;
EOF
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
    cp build/NoteApp "$PKG_DIR/usr/bin/speedynote"
    cp resources/icons/mainicon.png "$PKG_DIR/usr/share/pixmaps/speedynote.png"
    cp README.md "$PKG_DIR/usr/share/doc/$PKGNAME/"
    
    # Install translation files
    mkdir -p "$PKG_DIR/usr/share/speedynote/translations"
    if [ -d "resources/translations" ]; then
        cp resources/translations/*.qm "$PKG_DIR/usr/share/speedynote/translations/" 2>/dev/null || true
    fi
    
    # Create desktop file with PDF association
    create_desktop_file "$PKG_DIR/usr/share/applications/speedynote.desktop"
    
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
mkdir -p %{buildroot}/usr/share/doc/%{name}

install -m755 %{_vpath_builddir}/NoteApp %{buildroot}/usr/bin/speedynote
install -m644 resources/icons/mainicon.png %{buildroot}/usr/share/pixmaps/speedynote.png
install -m644 README.md %{buildroot}/usr/share/doc/%{name}/

# Install translation files
mkdir -p %{buildroot}/usr/share/speedynote/translations
if [ -d "resources/translations" ]; then
    cp resources/translations/*.qm %{buildroot}/usr/share/speedynote/translations/ 2>/dev/null || true
fi

# File manager integrations removed - using launcher instead

cat > %{buildroot}/usr/share/applications/speedynote.desktop << EOFDESKTOP
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
Keywords=notes;pdf;annotation;writing;
MimeType=application/pdf;
EOFDESKTOP

%post
/usr/bin/update-desktop-database -q /usr/share/applications || :

%postun
/usr/bin/update-desktop-database -q /usr/share/applications || :

%files
/usr/bin/speedynote
/usr/share/applications/speedynote.desktop
/usr/share/pixmaps/speedynote.png
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
license=('MIT')
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
    install -Dm755 "build/NoteApp" "\$pkgdir/usr/bin/speedynote"
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
Keywords=notes;pdf;annotation;writing;
MimeType=application/pdf;
EOFDESKTOP
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
create_apk_package() {
    echo -e "${YELLOW}Creating Alpine package...${NC}"
    
    # Create Alpine package structure
    mkdir -p alpine-pkg
    cd alpine-pkg
    
    # Create source tarball first to calculate checksum
    cd ..
    tar -czf "alpine-pkg/${PKGNAME}-${PKGVER}.tar.gz" \
        --exclude=build \
        --exclude=.git* \
        --exclude=alpine-pkg \
        --exclude="*.rpm" \
        --exclude="*.deb" \
        --exclude="*.pkg.tar.zst" \
        --exclude="*.apk" \
        .
    
    cd alpine-pkg
    
    # Calculate checksum
    CHECKSUM=$(sha256sum "${PKGNAME}-${PKGVER}.tar.gz" | cut -d' ' -f1)
    
    # Create APKBUILD
    cat > APKBUILD << EOF
# Maintainer: $MAINTAINER
pkgname=$PKGNAME
pkgver=$PKGVER
pkgrel=$PKGREL
pkgdesc="$DESCRIPTION"
url="$URL"
arch="all"
license="MIT"
depends="$(get_dependencies apk)"
makedepends="$(get_build_dependencies apk)"
source="\$pkgname-\$pkgver.tar.gz"
builddir="\$srcdir"
install="\$pkgname.post-install"
sha256sums="$CHECKSUM"

build() {
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # Limit parallelism to avoid OOM on systems with many cores but limited RAM
    local jobs=\$(( (\$(nproc) + 1) / 2 ))
    cmake --build build --parallel \$jobs
}

package() {
    install -Dm755 "build/NoteApp" "\$pkgdir/usr/bin/speedynote"
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
Keywords=notes;pdf;annotation;writing;
MimeType=application/pdf;
EOFDESKTOP
}
EOF
    
    # Create post-install script
    cat > "${PKGNAME}.post-install" << 'EOF'
#!/bin/sh

# Update desktop database
update-desktop-database -q /usr/share/applications 2>/dev/null || true

exit 0
EOF
    
    # Build package (source tarball already created above)
    abuild -r
    
    cd ..
    echo -e "${GREEN}Alpine package created in ~/packages/alpine-pkg/${PKGNAME}/ for .apk file"
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
                echo -e "Alpine: Check ~/packages/alpine-pkg/${PKGNAME}/ for .apk file"
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
    
    # Step 3: Build project (only needed for DEB which uses pre-built binary)
    # RPM, Arch, and Alpine all build from source in their respective build systems
    if [[ " ${PACKAGE_FORMATS[*]} " =~ " deb " ]]; then
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
