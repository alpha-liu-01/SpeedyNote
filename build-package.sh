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
PKGVER=$(sed -n 's/^project(SpeedyNote VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' CMakeLists.txt)
if [ -z "$PKGVER" ]; then
    echo "ERROR: Could not extract version from CMakeLists.txt"
    exit 1
fi
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

# Copy Qt's own translation catalogs (qtbase_<lang>.qm) for each language
# the app supports into the supplied destination dir. These hold the
# QMessageBox / QFileDialog standard button strings (Save / Discard /
# Cancel / Open / Yes / No / ...) that source/Main.cpp's translator loads
# alongside our app_<lang>.qm. Without them those buttons render in
# English even when the rest of the UI is translated.
#
# Filenames match upstream Qt 6: zh and pt only ship with region suffixes
# (qtbase_zh_CN.qm, qtbase_pt_BR.qm); the QLocale-aware loader probes the
# regional fallback chain automatically.
#
# Discovery order: qmake6 -> qmake -> /usr/share/qt6/translations ->
# /usr/share/qt5/translations -> /usr/share/qt/translations. Missing
# source files are warnings, not errors, so a build host that is missing
# some catalogs still produces a package.
copy_qt_translations() {
    local dest_dir="$1"
    if [[ -z "$dest_dir" ]]; then
        echo -e "${YELLOW}copy_qt_translations: missing destination${NC}"
        return 0
    fi

    local qt_tr_dir=""
    if command_exists qmake6; then
        qt_tr_dir=$(qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null || true)
    fi
    if [[ -z "$qt_tr_dir" || ! -d "$qt_tr_dir" ]] && command_exists qmake; then
        qt_tr_dir=$(qmake -query QT_INSTALL_TRANSLATIONS 2>/dev/null || true)
    fi
    for fallback in /usr/share/qt6/translations /usr/share/qt5/translations /usr/share/qt/translations; do
        if [[ -z "$qt_tr_dir" || ! -d "$qt_tr_dir" ]] && [[ -d "$fallback" ]]; then
            qt_tr_dir="$fallback"
        fi
    done

    if [[ -z "$qt_tr_dir" || ! -d "$qt_tr_dir" ]]; then
        echo -e "${YELLOW}WARNING: Qt translation prefix not found; standard dialog buttons may render in English.${NC}"
        return 0
    fi

    mkdir -p "$dest_dir"
    local copied=0
    # Keep in sync with the _SUPPORTED_QTBASE_QM list in CMakeLists.txt, the
    # $supportedQtbase array in compile.ps1, and the inline qtbase loops in
    # the rpm spec / Arch PKGBUILD / Alpine APKBUILD heredocs below.
    local qtbase_files=(qtbase_de.qm qtbase_es.qm qtbase_fr.qm qtbase_pt_BR.qm qtbase_zh_CN.qm qtbase_en.qm)
    for f in "${qtbase_files[@]}"; do
        if [[ -f "$qt_tr_dir/$f" ]]; then
            cp "$qt_tr_dir/$f" "$dest_dir/" 2>/dev/null && copied=$((copied + 1))
        else
            echo -e "${YELLOW}   (skip - not found in $qt_tr_dir: $f)${NC}"
        fi
    done
    echo -e "${CYAN}Copied $copied qtbase_*.qm catalog(s) from $qt_tr_dir${NC}"
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

# Function to get the *baseline* dependencies for each distribution.
# For .deb specifically, this is just Qt6 (with t64 alternates) — the
# rest of the runtime deps are computed dynamically from the actual
# binary contents by detect_deb_runtime_deps() below.
#
# Why dynamic detection: CMakeLists.txt picks static-vs-dynamic MuPDF
# based on what the build host has, which changes DT_NEEDED:
#   - libmupdf.so present (Debian 13+, Ubuntu 26.04+):
#       DT_NEEDED contains libmupdfXX.so.YY only; harfbuzz/freetype/etc.
#       come along transitively via libmupdf's own Depends.
#   - libmupdf.a only (Debian 12, Ubuntu 24.04 LTS):
#       MuPDF is embedded in the binary; DT_NEEDED instead lists
#       libharfbuzz.so.0, libfreetype.so.6, libjpeg.so.62,
#       libopenjp2.so.7, libjbig2dec.so.0, libgumbo.so.1 individually.
#
# Note on mujs: in CI we statically link our own libmujs.a (built from
# upstream source) so libmujs.so.X is never in DT_NEEDED at all — that
# avoids the libmujs2→libmujs3 SONAME break between Debian 12 and 13.
# See the "Build static libmujs from source" step in build-linux.yml.
# Local builds may still pick up the system's dynamic libmujs and add
# libmujs2/libmujs3 to Depends; that's correct for that build host.
#
# As of v1.2.1, SpeedyNote uses MuPDF exclusively (Poppler removed).
get_dependencies() {
    local format=$1
    case $format in
        deb)
            # Qt6 with t64 alternates only — additional deps are detected
            # from the binary by detect_deb_runtime_deps().
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
            # patchelf: clean the binary RUNPATH to $ORIGIN/../lib for the bundled ONNX Runtime
            echo "cmake, make, pkgconf, qt6-qtbase-devel, qt6-qttools-devel, mupdf-devel, harfbuzz-devel, freetype-devel, libjpeg-turbo-devel, openjpeg2-devel, jbig2dec-devel, gumbo-parser-devel, mujs-devel, patchelf"
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

# Function to detect the .deb's runtime Depends: from the actual built binary.
#
# Reads DT_NEEDED entries directly from the ELF header (via objdump -p),
# locates each SONAME in the multi-arch library directories, resolves
# symlinks, and queries dpkg -S to find the providing package. The result
# accurately reflects what the binary will load at runtime — regardless of
# whether MuPDF was statically or dynamically linked at build time.
#
# Why objdump and not ldd:
#   ldd actually invokes the dynamic linker to load the binary, which can
#   silently fail in CI containers, on cross-arch builds, or on systems
#   missing transitive libs — leaving us with empty/incomplete output and
#   no error. objdump -p reads ELF headers without any runtime, so it sees
#   every DT_NEEDED entry that's actually baked into the binary.
#
# Output: a comma-separated dependency string for the control file's
# Depends: field. Verbose progress is logged to stderr so CI logs make it
# obvious what was detected and what got mapped to a package.
#
# Exit status: 0 always; if no deps could be detected, falls back to the
# get_dependencies("deb") baseline. The caller should also run the audit
# step in build-linux.yml to verify Depends: covers every DT_NEEDED.
detect_deb_runtime_deps() {
    local binary="$1"
    local base_deps
    base_deps=$(get_dependencies deb)

    echo -e "${CYAN}  Analyzing $binary for runtime dependencies...${NC}" >&2

    if [[ ! -f "$binary" ]]; then
        echo -e "${RED}  ERROR: $binary not found — using static baseline${NC}" >&2
        echo "$base_deps"
        return
    fi
    if ! command_exists objdump; then
        echo -e "${YELLOW}  objdump unavailable — using static baseline${NC}" >&2
        echo "$base_deps"
        return
    fi
    if ! command_exists dpkg; then
        echo -e "${YELLOW}  dpkg unavailable — using static baseline${NC}" >&2
        echo "$base_deps"
        return
    fi

    # Extract DT_NEEDED SONAMEs (one per line). Robust to objdump format
    # variations: "  NEEDED               libfoo.so.X"
    local sonames
    sonames=$(objdump -p "$binary" 2>/dev/null \
            | awk '/^[[:space:]]*NEEDED[[:space:]]/ {print $2}')

    if [[ -z "$sonames" ]]; then
        echo -e "${YELLOW}  No DT_NEEDED entries found by objdump — using static baseline${NC}" >&2
        echo "$base_deps"
        return
    fi

    echo -e "${CYAN}  Found $(echo "$sonames" | wc -l) DT_NEEDED entries:${NC}" >&2
    for s in $sonames; do
        echo "    - $s" >&2
    done

    # Multi-arch-aware library search dirs. Order matters: the per-arch
    # GNU triplet dir is preferred so we get the right architecture's lib
    # on a multi-arch host.
    local arch_triplet
    arch_triplet=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null \
                   || gcc -print-multiarch 2>/dev/null \
                   || echo "")
    local -a search_dirs=()
    if [[ -n "$arch_triplet" ]]; then
        search_dirs+=("/usr/lib/$arch_triplet" "/lib/$arch_triplet")
    fi
    search_dirs+=("/usr/lib" "/usr/lib64" "/lib" "/lib64")

    local extra_deps=""
    declare -A seen_pkgs

    local soname
    for soname in $sonames; do
        # Skip libs that the base system / Qt baseline already covers.
        case "$soname" in
            libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|\
            libresolv.so.*|libnsl.so.*|libutil.so.*|ld-linux*.so.*|\
            libgcc_s.so.*|libstdc++.so.*)
                echo -e "${YELLOW}    [skip-base] $soname${NC}" >&2
                continue ;;
            libQt6Core.so.*|libQt6Gui.so.*|libQt6Widgets.so.*)
                echo -e "${YELLOW}    [skip-qt6 ] $soname (declared with t64 alternates)${NC}" >&2
                continue ;;
            libonnxruntime.so.*)
                # Vendored alongside the binary (usr/lib) and resolved via the
                # $ORIGIN/../lib RPATH — not a Debian package, so no Depends:.
                echo -e "${YELLOW}    [skip-ort ] $soname (vendored in usr/lib)${NC}" >&2
                continue ;;
        esac

        # Locate the SONAME on the filesystem.
        local lib_path=""
        local d
        for d in "${search_dirs[@]}"; do
            if [[ -e "$d/$soname" ]]; then
                lib_path="$d/$soname"
                break
            fi
        done
        if [[ -z "$lib_path" ]]; then
            echo -e "${RED}    [MISS-fs ] $soname — not found in any library dir; CHECK BUILD ENV${NC}" >&2
            continue
        fi

        # Resolve symlinks so dpkg -S finds the real file owner first.
        local real_path
        real_path=$(readlink -f "$lib_path" 2>/dev/null || echo "$lib_path")

        local owner_pkg
        owner_pkg=$(dpkg -S "$real_path" 2>/dev/null | head -1 | cut -d: -f1)
        if [[ -z "$owner_pkg" ]]; then
            # Some dev packages register the SONAME via the symlink path,
            # not the realpath; try the symlink as a fallback.
            owner_pkg=$(dpkg -S "$lib_path" 2>/dev/null | head -1 | cut -d: -f1)
        fi
        if [[ -z "$owner_pkg" ]]; then
            # usrmerge path mismatch: on Debian 12 the file lives at
            # /usr/lib/$ARCH/foo, but dpkg's database often registers it at
            # the pre-merge /lib/$ARCH/foo path. Glob by basename so dpkg
            # finds the package regardless of which path form was registered.
            local basename_only
            basename_only=$(basename "$real_path")
            owner_pkg=$(dpkg -S "*/$basename_only" 2>/dev/null | head -1 | cut -d: -f1)
        fi
        if [[ -z "$owner_pkg" ]]; then
            echo -e "${RED}    [MISS-pkg] $soname ($real_path) — no Debian package owns this${NC}" >&2
            continue
        fi
        # Strip :amd64 / :arm64 / :i386 architecture suffix.
        owner_pkg="${owner_pkg%%:*}"

        if [[ -n "${seen_pkgs[$owner_pkg]:-}" ]]; then
            echo -e "${CYAN}    [dedupe  ] $soname → $owner_pkg (already added)${NC}" >&2
            continue
        fi
        seen_pkgs[$owner_pkg]=1
        extra_deps="${extra_deps}, $owner_pkg"
        echo -e "${GREEN}    [   ok   ] $soname → $owner_pkg${NC}" >&2
    done

    echo -e "${CYAN}  Final Depends: ${base_deps}${extra_deps}${NC}" >&2
    echo "${base_deps}${extra_deps}"
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

# Map `uname -m` to the ORT_ARCH expected by linux/fetch-onnxruntime.sh.
ort_arch_for_host() {
    case "$(uname -m)" in
        x86_64|amd64)  echo "x64" ;;
        aarch64|arm64) echo "aarch64" ;;
        *)             echo "" ;;
    esac
}

# True when the vendored PaddleOCR dependencies (ONNX Runtime .so + the
# mandatory default model) are present on disk.
ocr_deps_present() {
    [[ -f linux/ocr-models/latin_rec.onnx ]] \
        && ls linux/onnxruntime-build/lib/libonnxruntime.so* >/dev/null 2>&1
}

# True when the vendored ONNX Runtime .so matches the host architecture.
# The prebuilt runtime is arch-specific, so a stale copy from another arch
# (e.g. the x64 default fetched by hand on an ARM box) would otherwise link
# and fail at ld with "file in wrong format". Returns 0 (ok) when we cannot
# tell (no objdump, unknown host) so we never block a legitimate build.
ort_vendored_arch_ok() {
    command -v objdump >/dev/null 2>&1 || return 0
    local lib elf
    lib=$(ls linux/onnxruntime-build/lib/libonnxruntime.so.* 2>/dev/null | head -1)
    [[ -n "$lib" ]] || lib=$(ls linux/onnxruntime-build/lib/libonnxruntime.so 2>/dev/null | head -1)
    [[ -n "$lib" ]] || return 0
    elf=$(objdump -f "$lib" 2>/dev/null | sed -n 's/^architecture: *\([^,]*\).*/\1/p')
    [[ -n "$elf" ]] || return 0
    case "$(uname -m)" in
        x86_64|amd64)  [[ "$elf" == *x86-64* ]] ;;
        aarch64|arm64) [[ "$elf" == *aarch64* ]] ;;
        *)             return 0 ;;
    esac
}

# Fetch the vendored ONNX Runtime + recognition models that PaddleOcrEngine
# needs. Both fetch scripts are idempotent (they skip when already present),
# but we still short-circuit here to avoid noisy re-runs. Used only for the
# glibc package formats (deb/rpm/arch) — the prebuilt ONNX Runtime is glibc
# and cannot run on Alpine/musl, so apk deliberately skips OCR entirely.
ensure_ocr_deps() {
    echo -e "${YELLOW}Ensuring OCR dependencies (ONNX Runtime + models)...${NC}"
    local ort_arch
    ort_arch="$(ort_arch_for_host)"
    if [[ -z "$ort_arch" ]]; then
        echo -e "${YELLOW}WARNING: unsupported architecture '$(uname -m)' for the prebuilt"
        echo -e "ONNX Runtime; OCR will be unavailable in this package.${NC}"
        return 0
    fi

    chmod +x linux/fetch-onnxruntime.sh linux/fetch-ocr-models.sh 2>/dev/null || true

    if [[ ! -f linux/onnxruntime-build/include/onnxruntime_cxx_api.h ]]; then
        ORT_ARCH="$ort_arch" ./linux/fetch-onnxruntime.sh
    elif ! ort_vendored_arch_ok; then
        echo -e "${YELLOW}Vendored ONNX Runtime is for a different architecture than"
        echo -e "the host ($(uname -m)); re-fetching the $ort_arch build.${NC}"
        rm -rf linux/onnxruntime-build
        ORT_ARCH="$ort_arch" ./linux/fetch-onnxruntime.sh
    else
        echo -e "${CYAN}ONNX Runtime already vendored.${NC}"
    fi
    if [[ ! -f linux/ocr-models/latin_rec.onnx ]]; then
        ./linux/fetch-ocr-models.sh
    else
        echo -e "${CYAN}OCR models already vendored.${NC}"
    fi
}

# Function to build the project.
# Arg 1: "off" to force-disable PaddleOCR (used by the apk/musl path); any
# other value lets CMake auto-enable it when the vendored deps are present.
build_project() {
    local ocr_mode="${1:-auto}"
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
    # Also ship Qt's own qtbase_<lang>.qm so standard dialog buttons translate.
    copy_qt_translations "build"
    
    cd build
    
    # Configure and build with optimizations
    echo -e "${YELLOW}Configuring build with maximum performance optimizations...${NC}"
    local ocr_opt=()
    if [[ "$ocr_mode" == "off" ]]; then
        # Alpine/musl: the prebuilt glibc ONNX Runtime is unusable here, so
        # keep PaddleOCR off regardless of any stale vendored dirs on disk.
        echo -e "${YELLOW}PaddleOCR explicitly disabled for this build (musl/apk).${NC}"
        ocr_opt=(-DSPEEDYNOTE_ENABLE_PADDLE_OCR=OFF)
    fi
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr "${ocr_opt[@]}" ..
    
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

    # Detect runtime deps from the actual binary (covers static vs dynamic
    # MuPDF linkage; see comment on detect_deb_runtime_deps for details).
    echo -e "${YELLOW}Detecting runtime dependencies from binary...${NC}"
    DEB_DEPENDS=$(detect_deb_runtime_deps build/speedynote)
    echo -e "${CYAN}  Depends: ${DEB_DEPENDS}${NC}"

    # Create control file
    cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: $PKGNAME
Version: $PKGVER-$PKGREL
Architecture: $(dpkg --print-architecture)
Maintainer: $MAINTAINER
Depends: ${DEB_DEPENDS}
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
    copy_qt_translations "$PKG_DIR/usr/share/speedynote/translations"
    
    # Install desktop file from committed source
    cp data/org.speedynote.SpeedyNote.desktop "$PKG_DIR/usr/share/applications/org.speedynote.SpeedyNote.desktop"

    # Bundle the vendored PaddleOCR runtime: the ONNX Runtime shared library
    # (resolved at runtime via the binary's $ORIGIN/../lib RPATH) and the
    # recognition models (found by PaddleOcrEngine at /usr/share/speedynote/
    # ocr-models). `cp -a` preserves the libonnxruntime.so -> .so.<ver> symlink
    # chain so the SONAME the binary needs is present without duplicating the
    # ~15 MB payload. The vendored .so is not a Debian package, so it is skipped
    # by detect_deb_runtime_deps() and the CI audit (no Depends: entry).
    if ocr_deps_present; then
        echo -e "${YELLOW}Bundling PaddleOCR runtime (ONNX Runtime + models)...${NC}"
        mkdir -p "$PKG_DIR/usr/lib"
        cp -a linux/onnxruntime-build/lib/libonnxruntime.so* "$PKG_DIR/usr/lib/"
        mkdir -p "$PKG_DIR/usr/share/speedynote/ocr-models"
        cp linux/ocr-models/*.onnx "$PKG_DIR/usr/share/speedynote/ocr-models/"
    else
        echo -e "${YELLOW}PaddleOCR deps not present; .deb will ship without OCR.${NC}"
    fi

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

    # Build the conditional PaddleOCR spec fragments. When the vendored ONNX
    # Runtime + models are present (fetched by ensure_ocr_deps for glibc
    # formats), the source tarball includes linux/onnxruntime-build and
    # linux/ocr-models, so rpmbuild can bundle them. The __requires_exclude /
    # __provides_exclude globals stop rpm's auto dep generator from emitting an
    # unsatisfiable `Requires: libonnxruntime.so.1` (no Fedora package owns the
    # vendored, RPATH-resolved .so).
    RPM_OCR_GLOBALS=""
    RPM_OCR_INSTALL=""
    RPM_OCR_FILES=""
    if ocr_deps_present; then
        read -r -d '' RPM_OCR_GLOBALS <<'SPECG' || true
# Vendored ONNX Runtime: bundled in /usr/lib, resolved via the binary RPATH.
%global __requires_exclude ^libonnxruntime\.so.*$
%global __provides_exclude ^libonnxruntime\.so.*$
# The binary intentionally carries a $ORIGIN/../lib RUNPATH so it can load the
# bundled libonnxruntime from /usr/lib. Fedora's check-rpaths BRP rejects any
# RPATH/RUNPATH by policy (and trips on the build-tree path CMake adds when
# linking the vendored .so), so disable it for this self-contained package.
# The runpath is also cleaned to exactly $ORIGIN/../lib in %install (patchelf).
%global __brp_check_rpaths %{nil}
SPECG
        read -r -d '' RPM_OCR_INSTALL <<'SPECI' || true

# Bundle the vendored PaddleOCR runtime (ONNX Runtime .so + recognition models)
mkdir -p %{buildroot}/usr/lib
cp -a linux/onnxruntime-build/lib/libonnxruntime.so* %{buildroot}/usr/lib/
mkdir -p %{buildroot}/usr/share/speedynote/ocr-models
cp linux/ocr-models/*.onnx %{buildroot}/usr/share/speedynote/ocr-models/
# Strip the absolute build-tree ONNX Runtime path (and the trailing empty
# entry) that CMake bakes into the build-tree binary's RUNPATH, leaving only
# the relocatable $ORIGIN/../lib (= /usr/lib at runtime).
patchelf --set-rpath '$ORIGIN/../lib' %{buildroot}/usr/bin/speedynote
SPECI
        read -r -d '' RPM_OCR_FILES <<'SPECF' || true
/usr/lib/libonnxruntime.so*
/usr/share/speedynote/ocr-models/
SPECF
    fi

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
${RPM_OCR_GLOBALS}

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
# Ship Qt's own qtbase_<lang>.qm catalogs so standard dialog buttons translate.
# All \$-variables are escaped so they reach the spec literally and are
# expanded by rpmbuild, not by the outer build-package.sh heredoc.
for f in qtbase_de.qm qtbase_es.qm qtbase_fr.qm qtbase_pt_BR.qm qtbase_zh_CN.qm qtbase_en.qm; do
    for tr_dir in \$(qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                  \$(qmake   -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                  /usr/share/qt6/translations /usr/share/qt5/translations \\
                  /usr/share/qt/translations; do
        if [ -f "\$tr_dir/\$f" ]; then
            cp "\$tr_dir/\$f" %{buildroot}/usr/share/speedynote/translations/ 2>/dev/null || true
            break
        fi
    done
done

# Install committed desktop file
install -Dm644 data/org.speedynote.SpeedyNote.desktop %{buildroot}/usr/share/applications/org.speedynote.SpeedyNote.desktop
${RPM_OCR_INSTALL}

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
${RPM_OCR_FILES}
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

    # Ship Qt's own qtbase_<lang>.qm catalogs so QMessageBox / QFileDialog
    # standard buttons (Save/Discard/Cancel/Open/...) translate together with
    # the rest of the UI. Escaped so makepkg, not the outer build script,
    # expands the variables.
    install -dm755 "\$pkgdir/usr/share/speedynote/translations"
    for f in qtbase_de.qm qtbase_es.qm qtbase_fr.qm qtbase_pt_BR.qm qtbase_zh_CN.qm qtbase_en.qm; do
        for tr_dir in \$(qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                      \$(qmake   -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                      /usr/share/qt6/translations /usr/share/qt5/translations \\
                      /usr/share/qt/translations; do
            if [ -f "\$tr_dir/\$f" ]; then
                install -m644 "\$tr_dir/\$f" "\$pkgdir/usr/share/speedynote/translations/"
                break
            fi
        done
    done

    # Install committed desktop file
    install -Dm644 "data/org.speedynote.SpeedyNote.desktop" "\$pkgdir/usr/share/applications/org.speedynote.SpeedyNote.desktop"

    # Bundle the vendored PaddleOCR runtime when present (the source tarball
    # includes linux/ only when fetch scripts ran on the build host). The .so is
    # resolved at runtime via the binary's \$ORIGIN/../lib RPATH; models are read
    # from /usr/share/speedynote/ocr-models.
    if [ -d "linux/onnxruntime-build/lib" ]; then
        install -dm755 "\$pkgdir/usr/lib"
        cp -a linux/onnxruntime-build/lib/libonnxruntime.so* "\$pkgdir/usr/lib/"
    fi
    if ls linux/ocr-models/*.onnx >/dev/null 2>&1; then
        install -dm755 "\$pkgdir/usr/share/speedynote/ocr-models"
        cp linux/ocr-models/*.onnx "\$pkgdir/usr/share/speedynote/ocr-models/"
    fi
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
#
# NOTE: PaddleOCR is intentionally NOT bundled for Alpine. The prebuilt ONNX
# Runtime from Microsoft is a glibc binary and cannot load on Alpine's musl
# libc, so the apk build runs `build_project off` (PaddleOCR disabled) and no
# ONNX Runtime / model files are shipped. OCR is simply unavailable on Alpine.
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

    # Ship Qt's own qtbase_<lang>.qm catalogs so QMessageBox / QFileDialog
    # standard buttons translate. Variables are escaped so abuild expands
    # them, not the outer build script.
    install -dm755 "\$pkgdir/usr/share/speedynote/translations"
    for f in qtbase_de.qm qtbase_es.qm qtbase_fr.qm qtbase_pt_BR.qm qtbase_zh_CN.qm qtbase_en.qm; do
        for tr_dir in \$(qmake6 -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                      \$(qmake   -query QT_INSTALL_TRANSLATIONS 2>/dev/null) \\
                      /usr/share/qt6/translations /usr/share/qt5/translations \\
                      /usr/share/qt/translations; do
            if [ -f "\$tr_dir/\$f" ]; then
                install -m644 "\$tr_dir/\$f" "\$pkgdir/usr/share/speedynote/translations/"
                break
            fi
        done
    done

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
    
    # Step 2c: Fetch the vendored PaddleOCR deps (ONNX Runtime + models) for the
    # glibc package formats that bundle them. RPM/Arch build from source, so the
    # vendored dirs must exist before their source tarballs are created (they
    # include linux/). apk is musl and deliberately excluded.
    if [[ " ${PACKAGE_FORMATS[*]} " =~ " deb " ]] \
       || [[ " ${PACKAGE_FORMATS[*]} " =~ " rpm " ]] \
       || [[ " ${PACKAGE_FORMATS[*]} " =~ " arch " ]]; then
        ensure_ocr_deps
    fi

    # Step 3: Build project (needed for DEB and APK which use pre-built binary)
    # RPM and Arch build from source in their respective build systems
    if [[ " ${PACKAGE_FORMATS[*]} " =~ " deb " ]]; then
        # deb is glibc and bundles OCR: let CMake auto-enable it.
        build_project auto
    elif [[ " ${PACKAGE_FORMATS[*]} " =~ " apk " ]]; then
        # apk-only: musl host, OCR forced off (prebuilt ORT is glibc).
        build_project off
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
