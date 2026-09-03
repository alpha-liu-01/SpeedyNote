#!/bin/bash
# =============================================================================
# Build MuPDF as a self-contained static library for macOS
# =============================================================================
# Produces libmupdf.a + libmupdf-third.a with every third-party dependency
# (freetype, harfbuzz, libjpeg, openjpeg, jbig2dec, lcms2, mujs, gumbo)
# compiled in, so the app bundle needs no Homebrew dylibs at all.
#
# Why this exists instead of `brew install mupdf`:
#   Homebrew bottles are built for the host OS release, so a Sonoma/Sequoia
#   bottle carries `minos 14.0`. Bundling those dylibs pinned SpeedyNote's
#   floor to macOS 14. A source build at -mmacosx-version-min=12.0 removes
#   every one of them and lets the app run on macOS 12.
#
# Prerequisites:
#   - Xcode command-line tools
#   - curl (comes with macOS)
#
# Usage (from the SpeedyNote project root):
#   ./macos/build-mupdf.sh
#
# Output:
#   macos/mupdf-build/lib/libmupdf.a
#   macos/mupdf-build/lib/libmupdf-third.a
#   macos/mupdf-build/include/mupdf/*.h
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
MUPDF_VERSION="1.24.10"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"

# macOS deployment target. Must match CMAKE_OSX_DEPLOYMENT_TARGET in
# CMakeLists.txt and MIN_MACOS_VERSION in compile-mac.sh.
# 12.0 is the floor: Qt 6.9's documented minimum is macOS 12.
MACOS_DEPLOYMENT_TARGET="12.0"

SHARED_SRC="${SCRIPT_DIR}/mupdf-src"
BUILD_DIR="${SCRIPT_DIR}/mupdf-build"

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            echo "Usage: $0"
            echo "  Builds static MuPDF ${MUPDF_VERSION} for macOS $(uname -m),"
            echo "  targeting macOS ${MACOS_DEPLOYMENT_TARGET}."
            exit 0
            ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# =============================================================================
# Toolchain detection
# =============================================================================
echo -e "${CYAN}=== Detecting macOS toolchain ===${NC}"

ARCH="$(uname -m)"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"
CC_BIN="$(xcrun --sdk macosx --find clang)"
CXX_BIN="$(xcrun --sdk macosx --find clang++)"
AR_BIN="$(xcrun --sdk macosx --find ar)"
RANLIB_BIN="$(xcrun --sdk macosx --find ranlib)"

echo "Arch:    ${ARCH}"
echo "SDK:     ${SDK_PATH}"
echo "CC:      ${CC_BIN}"
echo "CXX:     ${CXX_BIN}"
echo "AR:      ${AR_BIN}"
echo "RANLIB:  ${RANLIB_BIN}"
echo ""

if [ ! -d "${SDK_PATH}" ]; then
    echo -e "${RED}Error: macOS SDK not found. Install the Xcode command-line tools.${NC}"
    exit 1
fi

# =============================================================================
# Compilation flags
# =============================================================================
# -mmacosx-version-min is what stamps `minos` into every object file, and is
# the whole point of this script. MuPDF appends $(XCFLAGS) to its own CFLAGS
# (see its Makefile line 25), so passing them there reaches every compile.
MACOS_CFLAGS="-arch ${ARCH} -isysroot ${SDK_PATH} -mmacosx-version-min=${MACOS_DEPLOYMENT_TARGET} -fPIC -O2 -DNDEBUG"
MACOS_CXXFLAGS="${MACOS_CFLAGS}"
MACOS_LDFLAGS="-arch ${ARCH} -isysroot ${SDK_PATH} -mmacosx-version-min=${MACOS_DEPLOYMENT_TARGET}"

# =============================================================================
# Download and extract MuPDF
# =============================================================================
echo -e "${YELLOW}=== Building MuPDF ${MUPDF_VERSION} for macOS ${ARCH} ===${NC}"
echo "Deployment target: macOS ${MACOS_DEPLOYMENT_TARGET}"
echo ""

mkdir -p "${BUILD_DIR}"
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
# Build
# =============================================================================
cd "${SHARED_SRC}/mupdf-${MUPDF_VERSION}-source"

echo -e "${CYAN}=== Cleaning previous build ===${NC}"
make clean 2>/dev/null || true

# NOTE: unlike ios/build-mupdf.sh, the bundled HarfBuzz is NOT stripped here.
# That patch exists because iOS links Qt statically, so MuPDF's HarfBuzz and
# Qt's collide. On macOS Qt is a dynamic framework that keeps its own copy
# internal, and MuPDF's bundled HarfBuzz is already renamed to fzhb_* by
# thirdparty/harfbuzz/src/hb-rename.h. Keeping the build vanilla means one
# less divergence from upstream to maintain.

echo -e "${YELLOW}=== Compiling MuPDF for macOS ${ARCH} ===${NC}"

export CFLAGS="${MACOS_CFLAGS}"
export CXXFLAGS="${MACOS_CXXFLAGS}"
export LDFLAGS="${MACOS_LDFLAGS}"

# CC/AR/RANLIB are passed on the command line rather than exported: MuPDF's
# Makerules assigns `CC = xcrun cc` in its MACOS branch, and a makefile
# assignment beats an environment variable. Command-line variables beat both,
# so this is the only way to be sure which compiler ran.
#
# HAVE_LIBCRYPTO=no is deliberate. MuPDF auto-detects libcrypto through
# pkg-config, so on any machine with Homebrew OpenSSL installed (including the
# CI runners) the archive would come out referencing OpenSSL symbols that
# nothing links, and the app link would fail with undefined _EVP_* symbols.
# SpeedyNote never uses MuPDF's PDF signing, so switch it off explicitly and
# get the same archive on every machine.
#
# USE_SYSTEM_ZLIB=yes: libz is part of the macOS SDK on every supported
# release, so it stays a system dependency (CMakeLists.txt links `z`).
# Everything else is compiled from the bundled sources into libmupdf-third.a.
make \
    CC="${CC_BIN}" \
    CXX="${CXX_BIN}" \
    AR="${AR_BIN}" \
    RANLIB="${RANLIB_BIN}" \
    HAVE_X11=no \
    HAVE_GLUT=no \
    HAVE_CURL=no \
    HAVE_OBJCOPY=no \
    HAVE_LIBCRYPTO=no \
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
    XCFLAGS="${MACOS_CFLAGS}" \
    build=release \
    libs \
    -j"$(sysctl -n hw.ncpu)"

# =============================================================================
# Install libraries and headers
# =============================================================================
echo ""
echo -e "${CYAN}=== Installing libraries ===${NC}"

LIB_DIR="${BUILD_DIR}/lib"
mkdir -p "${LIB_DIR}"
cp build/release/libmupdf.a "${LIB_DIR}/"
cp build/release/libmupdf-third.a "${LIB_DIR}/"

echo -e "${CYAN}=== Installing headers ===${NC}"
rm -rf "${BUILD_DIR}/include/mupdf"
mkdir -p "${BUILD_DIR}/include/mupdf"
cp -r "${SHARED_SRC}/mupdf-${MUPDF_VERSION}-source/include/mupdf/"* "${BUILD_DIR}/include/mupdf/"

# =============================================================================
# Verification
# =============================================================================
# The deployment target is recorded per object file, and vtool only reads
# Mach-O files (an archive is not one), so unpack a member to inspect it.
# A wrong minos here is the failure mode this whole change exists to prevent,
# so treat it as fatal rather than printing a warning nobody reads.
verify_deployment_target() {
    local archive="$1"
    local name
    name="$(basename "${archive}")"

    local listing member probe_dir minos platform
    listing="$(ar t "${archive}")"
    member="$(printf '%s\n' "${listing}" | grep '\.o$' | head -1)"

    if [ -z "${member}" ]; then
        echo -e "${RED}  ${name}: no object members found${NC}"
        exit 1
    fi

    probe_dir="$(mktemp -d)"
    (cd "${probe_dir}" && ar x "${archive}" "${member}")

    minos="$(vtool -show-build "${probe_dir}/${member}" 2>/dev/null \
        | awk '/minos/ { print $2; exit }')"
    platform="$(vtool -show-build "${probe_dir}/${member}" 2>/dev/null \
        | awk '/platform/ { print $2; exit }')"
    rm -rf "${probe_dir}"

    if [ -z "${minos}" ]; then
        echo -e "${RED}  ${name}: could not read a deployment target from ${member}${NC}"
        exit 1
    fi

    if [ "${minos}" != "${MACOS_DEPLOYMENT_TARGET}" ]; then
        echo -e "${RED}  ${name}: minos ${minos} (expected ${MACOS_DEPLOYMENT_TARGET})${NC}"
        echo -e "${RED}  A library built for a newer macOS would raise the app's floor.${NC}"
        exit 1
    fi

    echo -e "${GREEN}  ${name}: platform ${platform}, minos ${minos} (via ${member})${NC}"
}

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
echo "Deployment target check:"
verify_deployment_target "${LIB_DIR}/libmupdf.a"
verify_deployment_target "${LIB_DIR}/libmupdf-third.a"
echo ""
echo "Headers:"
ls "${BUILD_DIR}/include/mupdf/" | head -10
echo ""
echo -e "${GREEN}=== Build Complete (macOS ${ARCH}, target ${MACOS_DEPLOYMENT_TARGET}) ===${NC}"
echo ""
echo "Directory layout:"
echo "  ${BUILD_DIR}/lib/libmupdf.a          (static library)"
echo "  ${BUILD_DIR}/lib/libmupdf-third.a    (third-party deps, compiled in)"
echo "  ${BUILD_DIR}/include/mupdf/*.h       (headers)"
