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
PKGVER="1.0.0"
PKGREL="1"
PKGARCH=$(uname -m)
MAINTAINER="SpeedyNote Team"
DESCRIPTION="A fast note-taking application with PDF annotation support and controller input"
URL="https://github.com/alpha-liu-01/SpeedyNote"
LICENSE="MIT"

# Default values
DISTRO=""
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
            echo "cmake, make, pkg-config, qt6-base-dev, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, qt6-tools-dev, libpoppler-qt6-dev, libsdl2-dev, libasound2-dev"
            ;;
        rpm)
            echo "cmake, make, pkgconf, qt6-qtbase-devel, qt6-qttools-devel, poppler-qt6-devel, SDL2-devel, alsa-lib-devel"
            ;;
        arch)
            echo "cmake, make, pkgconf, qt6-base, qt6-tools, poppler-qt6, sdl2-compat, alsa-lib"
            ;;
        apk)
            echo "cmake, make, pkgconf, qt6-qtbase-dev, qt6-qttools-dev, poppler-qt6, poppler-qt5-dev, sdl2-dev, alsa-lib-dev"
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
    
    # Compile translations if lrelease is available
    if command_exists lrelease; then
        echo -e "${YELLOW}Compiling translation files...${NC}"
        # lrelease ./resources/translations/app_zh.ts ./resources/translations/app_fr.ts ./resources/translations/app_es.ts
        cp resources/translations/*.qm build/ 2>/dev/null || true
    fi
    
    cd build
    
    # Configure and build with optimizations
    echo -e "${YELLOW}Configuring build with maximum performance optimizations...${NC}"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
    
    echo -e "${YELLOW}Compiling with $(nproc) parallel jobs...${NC}"
    make -j$(nproc)
    
    if [[ ! -f "NoteApp" ]]; then
        echo -e "${RED}Build failed: NoteApp executable not found${NC}"
        exit 1
    fi
    
    cd ..
    echo -e "${GREEN}Build successful!${NC}"
}

# Function to create desktop file with PDF and SPN MIME type association
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
Keywords=notes;pdf;annotation;writing;package;
MimeType=application/pdf;application/x-speedynote-package;
EOF
}

# Function to create PDF-to-SPN desktop action file (REMOVED - using launcher instead)
# create_pdf_action_file() {
#     local action_file="$1"
#     cat > "$action_file" << EOF
# [Desktop Entry]
# Version=1.0
# Type=Application
# Name=Create SpeedyNote Package
# Comment=Create a SpeedyNote package from PDF
# Exec=speedynote --create-new %F
# Icon=speedynote
# Terminal=false
# StartupNotify=true
# Categories=Office;Education;
# Keywords=notes;pdf;annotation;package;create;
# MimeType=application/pdf;
# NoDisplay=true
# EOF
# }

# Function to create SPN template file for "New" context menu (REMOVED - using launcher instead)
# create_spn_template() {
#     local template_file="$1"
#     # Create a minimal .spn template file that file managers can copy
#     echo "Contents" > "$template_file"
# }

# Function to create file templates for "New" context menu (REMOVED - using launcher instead)
# create_file_templates() {
#     local pkg_dir="$1"
#     
#     # Create template directory
#     mkdir -p "$pkg_dir/usr/share/templates"
#     
#     # Create .spn template file
#     create_spn_template "$pkg_dir/usr/share/templates/Empty SpeedyNote Package.spn"
#     
#     # Create desktop file for template (KDE)
#     cat > "$pkg_dir/usr/share/templates/Empty SpeedyNote Package.desktop" << 'EOF'
# [Desktop Entry]
# Name=SpeedyNote Package
# Name[es]=Paquete SpeedyNote
# Name[fr]=Package SpeedyNote
# Name[zh]=SpeedyNote 包
# Comment=Create a new SpeedyNote package
# Comment[es]=Crear un nuevo paquete SpeedyNote
# Comment[fr]=Créer un nouveau package SpeedyNote
# Comment[zh]=创建新的 SpeedyNote 包
# URL=Empty SpeedyNote Package.spn
# Type=Link
# Icon=speedynote
# EOF
# 
#     # Create Nautilus template
#     mkdir -p "$pkg_dir/usr/share/nautilus-python/extensions"
#     cat > "$pkg_dir/usr/share/nautilus-python/extensions/speedynote_template.py" << 'EOF'
# #!/usr/bin/env python3
# """
# Nautilus extension to add SpeedyNote Package to "New Document" menu
# """
# 
# import os
# import subprocess
# from gi.repository import Nautilus, GObject, Gio
# 
# class SpeedyNoteTemplateProvider(GObject.GObject, Nautilus.MenuProvider):
#     def __init__(self):
#         super().__init__()
# 
#     def get_background_items(self, window, current_folder):
#         """Add SpeedyNote Package to right-click background menu"""
#         if not current_folder:
#             return []
# 
#         # Create menu item
#         item = Nautilus.MenuItem(
#             name='SpeedyNoteTemplate::create_spn',
#             label='SpeedyNote Package',
#             tip='Create a new SpeedyNote package'
#         )
#         
#         # Connect callback
#         item.connect('activate', self._create_spn_package, current_folder)
#         
#         # Create submenu for "New Document"
#         submenu = Nautilus.Menu()
#         submenu.append_item(item)
#         
#         # Create parent menu item
#         parent_item = Nautilus.MenuItem(
#             name='SpeedyNoteTemplate::new_document',
#             label='New Document',
#             tip='Create new documents'
#         )
#         parent_item.set_submenu(submenu)
#         
#         return [parent_item]
# 
#     def _create_spn_package(self, menu, current_folder):
#         """Create a new SpeedyNote package"""
#         folder_path = current_folder.get_location().get_path()
#         
#         # Generate unique filename
#         base_name = "New SpeedyNote Package"
#         counter = 1
#         spn_path = os.path.join(folder_path, f"{base_name}.spn")
#         
#         while os.path.exists(spn_path):
#             spn_path = os.path.join(folder_path, f"{base_name} {counter}.spn")
#             counter += 1
#         
#         try:
#             # Create SpeedyNote package using silent mode
#             subprocess.run(['speedynote', '--create-silent', spn_path], check=True)
#         except (subprocess.CalledProcessError, FileNotFoundError):
#             # Fallback: create minimal template
#             with open(spn_path, 'w') as f:
#                 f.write("Contents")
# EOF
#     chmod +x "$pkg_dir/usr/share/nautilus-python/extensions/speedynote_template.py"
# }

# Function to create MIME type definition for .spn files
create_mime_xml() {
    local mime_file="$1"
    cat > "$mime_file" << 'EOF'
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
EOF
}

# Function to create file manager integration scripts (REMOVED - using launcher instead)
# create_file_manager_integration() {
#     local pkg_dir="$1"
#     
#     # Create Nautilus (GNOME) script for "Create SpeedyNote Package"
#     mkdir -p "$pkg_dir/usr/share/nautilus/scripts"
#     cat > "$pkg_dir/usr/share/nautilus/scripts/Create SpeedyNote Package" << 'EOF'
# #!/bin/bash
# # Nautilus script to create SpeedyNote package from PDF
# 
# if [ -n "$NAUTILUS_SCRIPT_SELECTED_FILE_PATHS" ]; then
#     while IFS= read -r file; do
#         if [[ "$file" == *.pdf ]]; then
#             # Extract directory and filename
#             dir=$(dirname "$file")
#             basename=$(basename "$file" .pdf)
#             spn_path="$dir/$basename.spn"
#             
#             # Create SpeedyNote package
#             speedynote --create-silent "$spn_path" "$file"
#         fi
#     done <<< "$NAUTILUS_SCRIPT_SELECTED_FILE_PATHS"
# fi
# EOF
#     chmod +x "$pkg_dir/usr/share/nautilus/scripts/Create SpeedyNote Package"
#     
#     # Create Dolphin (KDE) service menu
#     mkdir -p "$pkg_dir/usr/share/kservices5/ServiceMenus"
#     cat > "$pkg_dir/usr/share/kservices5/ServiceMenus/speedynote-create-package.desktop" << 'EOF'
# [Desktop Entry]
# Type=Service
# X-KDE-ServiceTypes=KonqPopupMenu/Plugin
# MimeType=application/pdf;
# Actions=CreateSpeedyNotePackage;
# 
# [Desktop Action CreateSpeedyNotePackage]
# Name=Create SpeedyNote Package
# Name[es]=Crear Paquete SpeedyNote
# Name[fr]=Créer un Package SpeedyNote
# Name[zh]=创建 SpeedyNote 包
# Icon=speedynote
# Exec=sh -c 'dir=$(dirname "%f"); base=$(basename "%f" .pdf); speedynote --create-silent "$dir/$base.spn" "%f"'
# EOF
#     
#     # Create Thunar (XFCE) custom action
#     mkdir -p "$pkg_dir/usr/share/Thunar/sendto"
#     cat > "$pkg_dir/usr/share/Thunar/sendto/speedynote-create-package.desktop" << 'EOF'
# [Desktop Entry]
# Version=1.0
# Type=Application
# Name=Create SpeedyNote Package
# Name[es]=Crear Paquete SpeedyNote
# Name[fr]=Créer un Package SpeedyNote
# Name[zh]=创建 SpeedyNote 包
# Comment=Create a SpeedyNote package from PDF
# Icon=speedynote
# Exec=sh -c 'for file in %F; do if [[ "$file" == *.pdf ]]; then dir=$(dirname "$file"); base=$(basename "$file" .pdf); speedynote --create-silent "$dir/$base.spn" "$file"; fi; done'
# MimeType=application/pdf;
# Categories=Office;
# EOF
# 
#     # Create PCManFM (LXDE/LXQt) action
#     mkdir -p "$pkg_dir/usr/share/file-manager/actions"
#     cat > "$pkg_dir/usr/share/file-manager/actions/speedynote-create-package.desktop" << 'EOF'
# [Desktop Entry]
# Type=Action
# Name=Create SpeedyNote Package
# Name[es]=Crear Paquete SpeedyNote
# Name[fr]=Créer un Package SpeedyNote
# Name[zh]=创建 SpeedyNote 包
# Comment=Create a SpeedyNote package from PDF
# Icon=speedynote
# Profiles=profile-zero;
# 
# [X-Action-Profile profile-zero]
# MimeTypes=application/pdf;
# Exec=sh -c 'for file in %F; do if [[ "$file" == *.pdf ]]; then dir=$(dirname "$file"); base=$(basename "$file" .pdf); speedynote --create-silent "$dir/$base.spn" "$file"; fi; done'
# Name=Create SpeedyNote Package
# EOF
# }

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
    mkdir -p "$PKG_DIR/usr/share/mime/packages"
    
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
 SpeedyNote is a fast and efficient note-taking application with PDF annotation
 support and controller input capabilities.
EOF
    
    # Create postinst script for MIME database update
    cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Update MIME database
if [ -x /usr/bin/update-mime-database ]; then
    update-mime-database /usr/share/mime
fi

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
    # Update MIME database
    if [ -x /usr/bin/update-mime-database ]; then
        update-mime-database /usr/share/mime
    fi
    
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
    
    # Create MIME type definition for .spn files
    create_mime_xml "$PKG_DIR/usr/share/mime/packages/application-x-speedynote-package.xml"
    
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
SpeedyNote is a fast and efficient note-taking application with PDF annotation
support and controller input capabilities.

%prep
%setup -q

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/share/pixmaps
mkdir -p %{buildroot}/usr/share/doc/%{name}
mkdir -p %{buildroot}/usr/share/mime/packages

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
Keywords=notes;pdf;annotation;writing;package;
MimeType=application/pdf;application/x-speedynote-package;
EOFDESKTOP

cat > %{buildroot}/usr/share/mime/packages/application-x-speedynote-package.xml << EOFMIME
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

%post
/usr/bin/update-desktop-database -q /usr/share/applications || :
/usr/bin/update-mime-database /usr/share/mime &> /dev/null || :

%postun
/usr/bin/update-desktop-database -q /usr/share/applications || :
/usr/bin/update-mime-database /usr/share/mime &> /dev/null || :

%files
/usr/bin/speedynote
/usr/share/applications/speedynote.desktop
/usr/share/pixmaps/speedynote.png
/usr/share/doc/%{name}/README.md
/usr/share/mime/packages/application-x-speedynote-package.xml
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
    
    # Create source tarball
    tar -czf "${PKGNAME}-${PKGVER}.tar.gz" \
        --exclude=build \
        --exclude=.git* \
        --exclude="*.tar.gz" \
        --exclude="*.pkg.tar.zst" \
        --exclude=pkg \
        --exclude=src \
        .
    
    # Create PKGBUILD
    cat > PKGBUILD << EOF
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
    cd "\$srcdir"
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build --parallel
}

package() {
    cd "\$srcdir"
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
Keywords=notes;pdf;annotation;writing;package;
MimeType=application/pdf;application/x-speedynote-package;
EOFDESKTOP

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

post_install() {
    update-desktop-database -q
    update-mime-database /usr/share/mime
}

post_upgrade() {
    update-desktop-database -q
    update-mime-database /usr/share/mime
}

post_remove() {
    update-desktop-database -q
    update-mime-database /usr/share/mime
}
EOF
    
    # Build package
    makepkg -f
    
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
    cmake --build build --parallel
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
Keywords=notes;pdf;annotation;writing;package;
MimeType=application/pdf;application/x-speedynote-package;
EOFDESKTOP

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
    
    # Create post-install script
    cat > "${PKGNAME}.post-install" << 'EOF'
#!/bin/sh

# Update desktop and MIME databases
update-desktop-database -q /usr/share/applications 2>/dev/null || true
update-mime-database /usr/share/mime 2>/dev/null || true

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
    rm -rf build debian-pkg alpine-pkg
    rm -f "${PKGNAME}-${PKGVER}.tar.gz"
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
    echo -e "✅ .spn Package Association: Double-click .spn files to open in SpeedyNote"
    echo -e "✅ Launcher Integration: Use SpeedyNote launcher for creating new packages and managing documents"
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
    
    # Step 3: Build project
    build_project
    
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
