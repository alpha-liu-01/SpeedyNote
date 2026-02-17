#!/bin/bash
# =============================================================================
# Cross-compile MuPDF for iOS arm64
# =============================================================================
# This script builds libmupdf and its dependencies as static libraries
# for iOS ARM64, using Xcode's clang and the iPhoneOS SDK.
#
# Prerequisites:
#   - Xcode 15+ with command-line tools
#   - curl (comes with macOS)
#
# Run from the SpeedyNote project root:
#   ./ios/build-mupdf.sh
#
# Output:
#   ios/mupdf-build/lib/libmupdf.a
#   ios/mupdf-build/lib/libmupdf-third.a
#   ios/mupdf-build/include/mupdf/*.h
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
BUILD_DIR="${SCRIPT_DIR}/mupdf-build"
MUPDF_VERSION="1.24.10"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"

# iOS deployment target (must match CMakeLists.txt)
IOS_DEPLOYMENT_TARGET="15.0"

# =============================================================================
# Toolchain detection via xcrun
# =============================================================================
echo -e "${CYAN}=== Detecting iOS toolchain ===${NC}"

IOS_SDK_PATH="$(xcrun --sdk iphoneos --show-sdk-path)"
CC="$(xcrun --sdk iphoneos --find clang)"
CXX="$(xcrun --sdk iphoneos --find clang++)"
AR="$(xcrun --sdk iphoneos --find ar)"
RANLIB="$(xcrun --sdk iphoneos --find ranlib)"

echo "SDK:     ${IOS_SDK_PATH}"
echo "CC:      ${CC}"
echo "CXX:     ${CXX}"
echo "AR:      ${AR}"
echo "RANLIB:  ${RANLIB}"
echo ""

if [ ! -d "${IOS_SDK_PATH}" ]; then
    echo -e "${RED}Error: iPhoneOS SDK not found. Is Xcode installed?${NC}"
    exit 1
fi

# =============================================================================
# Cross-compilation flags
# =============================================================================
IOS_CFLAGS="-arch arm64 -isysroot ${IOS_SDK_PATH} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET} -fPIC -O2 -DNDEBUG"
IOS_CXXFLAGS="${IOS_CFLAGS}"
IOS_LDFLAGS="-arch arm64 -isysroot ${IOS_SDK_PATH} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}"

# =============================================================================
# Download and extract MuPDF
# =============================================================================
echo -e "${YELLOW}=== Building MuPDF ${MUPDF_VERSION} for iOS arm64 ===${NC}"
echo "Deployment target: iPadOS ${IOS_DEPLOYMENT_TARGET}"
echo ""

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

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
cd "${BUILD_DIR}/mupdf-${MUPDF_VERSION}-source"

echo -e "${CYAN}=== Cleaning previous build ===${NC}"
make clean 2>/dev/null || true

echo -e "${YELLOW}=== Compiling MuPDF for iOS arm64 ===${NC}"

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

echo -e "${CYAN}=== Installing headers ===${NC}"
mkdir -p "${BUILD_DIR}/include/mupdf"
cp -r "${BUILD_DIR}/mupdf-${MUPDF_VERSION}-source/include/mupdf/"* "${BUILD_DIR}/include/mupdf/"

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
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "Directory layout:"
echo "  ios/mupdf-build/lib/libmupdf.a          (static library)"
echo "  ios/mupdf-build/lib/libmupdf-third.a    (third-party deps)"
echo "  ios/mupdf-build/include/mupdf/*.h        (headers)"
