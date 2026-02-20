#!/bin/bash
# =============================================================================
# Cross-compile MuPDF for iOS (device arm64 or simulator x86_64)
# =============================================================================
# This script builds libmupdf and its dependencies as static libraries
# for iOS, using Xcode's clang and the appropriate SDK.
#
# Prerequisites:
#   - Xcode 15+ with command-line tools
#   - curl (comes with macOS)
#
# Usage (from the SpeedyNote project root):
#   ./ios/build-mupdf.sh               # Build for device (arm64)
#   ./ios/build-mupdf.sh --simulator   # Build for simulator (x86_64)
#
# Output (device):
#   ios/mupdf-build/lib/libmupdf.a
#   ios/mupdf-build/lib/libmupdf-third.a
#   ios/mupdf-build/include/mupdf/*.h
#
# Output (simulator):
#   ios/mupdf-build-sim/lib/libmupdf.a
#   ios/mupdf-build-sim/lib/libmupdf-third.a
#   ios/mupdf-build-sim/include/mupdf/*.h
#
# =============================================================================
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
MUPDF_VERSION="1.24.10"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"

# iOS deployment target (must match CMakeLists.txt)
IOS_DEPLOYMENT_TARGET="16.0"

# =============================================================================
# Parse arguments
# =============================================================================
BUILD_SIMULATOR=false
for arg in "$@"; do
    case "$arg" in
        --simulator) BUILD_SIMULATOR=true ;;
        -h|--help)
            echo "Usage: $0 [--simulator]"
            echo "  --simulator   Build for iOS Simulator (x86_64)"
            echo "  (default)     Build for iOS device (arm64)"
            exit 0
            ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# =============================================================================
# Configure for device or simulator
# =============================================================================
if [ "${BUILD_SIMULATOR}" = true ]; then
    SDK_NAME="iphonesimulator"
    ARCH="x86_64"
    MIN_VERSION_FLAG="-mios-simulator-version-min=${IOS_DEPLOYMENT_TARGET}"
    BUILD_DIR="${SCRIPT_DIR}/mupdf-build-sim"
    LABEL="iOS Simulator x86_64"
else
    SDK_NAME="iphoneos"
    ARCH="arm64"
    MIN_VERSION_FLAG="-miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}"
    BUILD_DIR="${SCRIPT_DIR}/mupdf-build"
    LABEL="iOS device arm64"
fi

# =============================================================================
# Toolchain detection via xcrun
# =============================================================================
echo -e "${CYAN}=== Detecting ${LABEL} toolchain ===${NC}"

IOS_SDK_PATH="$(xcrun --sdk ${SDK_NAME} --show-sdk-path)"
CC="$(xcrun --sdk ${SDK_NAME} --find clang)"
CXX="$(xcrun --sdk ${SDK_NAME} --find clang++)"
AR="$(xcrun --sdk ${SDK_NAME} --find ar)"
RANLIB="$(xcrun --sdk ${SDK_NAME} --find ranlib)"

echo "SDK:     ${IOS_SDK_PATH}"
echo "CC:      ${CC}"
echo "CXX:     ${CXX}"
echo "AR:      ${AR}"
echo "RANLIB:  ${RANLIB}"
echo ""

if [ ! -d "${IOS_SDK_PATH}" ]; then
    echo -e "${RED}Error: ${SDK_NAME} SDK not found. Is Xcode installed?${NC}"
    exit 1
fi

# =============================================================================
# Cross-compilation flags
# =============================================================================
IOS_CFLAGS="-arch ${ARCH} -isysroot ${IOS_SDK_PATH} ${MIN_VERSION_FLAG} -fPIC -O2 -DNDEBUG"
IOS_CXXFLAGS="${IOS_CFLAGS}"
IOS_LDFLAGS="-arch ${ARCH} -isysroot ${IOS_SDK_PATH} ${MIN_VERSION_FLAG}"

# =============================================================================
# Download and extract MuPDF (shared source between device and simulator)
# =============================================================================
echo -e "${YELLOW}=== Building MuPDF ${MUPDF_VERSION} for ${LABEL} ===${NC}"
echo "Deployment target: iPadOS ${IOS_DEPLOYMENT_TARGET}"
echo ""

mkdir -p "${BUILD_DIR}"

# Use a shared source directory to avoid downloading twice
SHARED_SRC="${SCRIPT_DIR}/mupdf-src"
mkdir -p "${SHARED_SRC}"
cd "${SHARED_SRC}"

if [ ! -d "mupdf-${MUPDF_VERSION}-source" ]; then
    echo -e "${CYAN}=== Downloading MuPDF ${MUPDF_VERSION} ===${NC}"
    curl -L -o "mupdf-${MUPDF_VERSION}-source.tar.gz" "${MUPDF_URL}"

    echo -e "${CYAN}=== Extracting ===${NC}"
    tar xzf "mupdf-${MUPDF_VERSION}-source.tar.gz"
    rm "mupdf-${MUPDF_VERSION}-source.tar.gz"
fi

# =============================================================================
# Build MuPDF
# =============================================================================
cd "${SHARED_SRC}/mupdf-${MUPDF_VERSION}-source"

echo -e "${CYAN}=== Cleaning previous build ===${NC}"
make clean 2>/dev/null || true

# =============================================================================
# Patch: Strip bundled HarfBuzz from MuPDF
# =============================================================================
# MuPDF bundles its own HarfBuzz with custom allocators that require an
# fz_context*. Qt also bundles HarfBuzz. When both are linked, internal C++
# symbols collide and Qt ends up calling MuPDF's HarfBuzz code without the
# required context, causing a NULL-pointer crash in fz_calloc_no_throw.
#
# SpeedyNote only uses MuPDF for PDF rendering/export — never the HTML/EPUB
# engine (source/html/html-layout.c), which is the only consumer of HarfBuzz
# in MuPDF. So we simply don't compile HarfBuzz at all.
#
# By appending "HARFBUZZ_SRC :=" to Makelists, we clear the source file list
# so none of the thirdparty/harfbuzz/src/*.cc files are compiled. The include
# path remains so html-layout.c still compiles, but its undefined fzhb_*
# references are harmless — the linker never pulls in html-layout.o because
# SpeedyNote never calls fz_layout_html() or fz_draw_html().
# =============================================================================
echo -e "${CYAN}=== Patching MuPDF: removing bundled HarfBuzz ===${NC}"

# Step 1: Don't compile the bundled HarfBuzz .cc source files
if ! grep -q "^HARFBUZZ_SRC :=$" Makelists; then
    echo "" >> Makelists
    echo "# iOS patch: disable bundled HarfBuzz (conflicts with Qt's copy)" >> Makelists
    echo "HARFBUZZ_SRC :=" >> Makelists
    echo "  Patched Makelists to disable HarfBuzz compilation"
else
    echo "  Makelists already patched"
fi

# Step 2: Empty the rename header so MuPDF's HTML code calls hb_* (not fzhb_*).
#         The unrenamed hb_* symbols resolve to Qt's bundled HarfBuzz at link time.
RENAME_H="thirdparty/harfbuzz/src/hb-rename.h"
if [ -f "${RENAME_H}" ] && grep -q "fzhb_" "${RENAME_H}"; then
    cat > "${RENAME_H}" <<'HEADER_EOF'
/* iOS patch: rename macros removed so MuPDF's HTML code uses Qt's HarfBuzz */
#ifndef HB_RENAME_H
#define HB_RENAME_H
#endif
HEADER_EOF
    echo "  Cleared hb-rename.h (hb_* calls will resolve to Qt's HarfBuzz)"
else
    echo "  hb-rename.h already patched"
fi

# Step 3: Add a stub for hb_ft_font_create in harfbuzz.c.
#         Qt's HarfBuzz doesn't include the hb-ft (FreeType bridge) module.
#         html-layout.c references it, but SpeedyNote never calls that code path.
HARFBUZZ_C="source/fitz/harfbuzz.c"
if ! grep -q "hb_ft_font_create" "${HARFBUZZ_C}"; then
    cat >> "${HARFBUZZ_C}" <<'STUB_EOF'

/* iOS stub: html-layout.o references hb_ft_font_create (FreeType-HarfBuzz bridge)
   but Qt's HarfBuzz doesn't include the hb-ft module. SpeedyNote only uses MuPDF
   for PDF operations, never HTML/EPUB layout, so this is never actually called. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include "hb-ft.h"

hb_font_t *hb_ft_font_create(FT_Face ft_face, hb_destroy_func_t destroy)
{
	(void)ft_face;
	(void)destroy;
	return NULL;
}
STUB_EOF
    echo "  Added hb_ft_font_create stub to harfbuzz.c"
else
    echo "  harfbuzz.c stub already present"
fi

echo -e "${YELLOW}=== Compiling MuPDF for ${LABEL} ===${NC}"

export CC="${CC}"
export CXX="${CXX}"
export AR="${AR}"
export RANLIB="${RANLIB}"
export CFLAGS="${IOS_CFLAGS}"
export CXXFLAGS="${IOS_CXXFLAGS}"
export LDFLAGS="${IOS_LDFLAGS}"

make \
    HAVE_X11=no \
    HAVE_GLUT=no \
    HAVE_CURL=no \
    HAVE_OBJCOPY=no \
    USE_SYSTEM_FREETYPE=no \
    USE_SYSTEM_HARFBUZZ=no \
    USE_SYSTEM_LIBJPEG=no \
    USE_SYSTEM_ZLIB=yes \
    USE_SYSTEM_OPENJPEG=no \
    USE_SYSTEM_JBIG2DEC=no \
    USE_SYSTEM_LCMS2=no \
    USE_SYSTEM_MUJS=no \
    USE_SYSTEM_GUMBO=no \
    USE_SYSTEM_LEPTONICA=no \
    USE_SYSTEM_TESSERACT=no \
    shared=no \
    verbose=yes \
    XCFLAGS="${IOS_CFLAGS}" \
    build=release \
    libs \
    -j$(sysctl -n hw.ncpu)

# =============================================================================
# Install libraries and headers
# =============================================================================
echo ""
echo -e "${CYAN}=== Installing libraries ===${NC}"

LIB_DIR="${BUILD_DIR}/lib"
mkdir -p "${LIB_DIR}"
cp build/release/libmupdf.a "${LIB_DIR}/"
cp build/release/libmupdf-third.a "${LIB_DIR}/"

# Verify that no HarfBuzz symbols leaked into the third-party library
HB_SYMBOLS=$(nm -g "${LIB_DIR}/libmupdf-third.a" 2>/dev/null | grep " [TD] " | grep "_hb_\|_fzhb_" | wc -l | tr -d ' ')
echo "  HarfBuzz symbols in libmupdf-third.a: ${HB_SYMBOLS} (should be 0)"

echo -e "${CYAN}=== Installing headers ===${NC}"
mkdir -p "${BUILD_DIR}/include/mupdf"
cp -r "${SHARED_SRC}/mupdf-${MUPDF_VERSION}-source/include/mupdf/"* "${BUILD_DIR}/include/mupdf/"

# =============================================================================
# Verification
# =============================================================================
echo ""
echo -e "${YELLOW}=== Verification ===${NC}"
echo ""
echo "Libraries:"
ls -la "${LIB_DIR}/"
echo ""
echo "Architecture check:"
lipo -info "${LIB_DIR}/libmupdf.a"
lipo -info "${LIB_DIR}/libmupdf-third.a"
echo ""
echo "Headers:"
ls "${BUILD_DIR}/include/mupdf/" | head -10
echo ""
echo -e "${GREEN}=== Build Complete (${LABEL}) ===${NC}"
echo ""
echo "Directory layout:"
echo "  ${BUILD_DIR}/lib/libmupdf.a          (static library)"
echo "  ${BUILD_DIR}/lib/libmupdf-third.a    (third-party deps)"
echo "  ${BUILD_DIR}/include/mupdf/*.h        (headers)"
