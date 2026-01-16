#!/bin/bash
# =============================================================================
# Cross-compile MuPDF for Android arm64-v8a
# =============================================================================
# This script builds libmupdf and its dependencies for Android using the NDK.
#
# Run inside the Docker container:
#   ./android/build-mupdf.sh
#
# Output:
#   android/mupdf-build/lib/libmupdf.a
#   android/mupdf-build/lib/libmupdf-third.a
#   android/mupdf-build/include/mupdf/*.h
#
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${SCRIPT_DIR}/mupdf-build"
MUPDF_VERSION="1.24.10"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"

# Android NDK settings (from Docker environment)
ANDROID_NDK="${ANDROID_NDK_ROOT:-/opt/android-sdk/ndk/26.1.10909125}"
ANDROID_API="${ANDROID_MIN_SDK:-26}"
ANDROID_ABI="arm64-v8a"
ANDROID_ARCH="aarch64"

# NDK toolchain paths
TOOLCHAIN="${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64"
SYSROOT="${TOOLCHAIN}/sysroot"

# Cross-compilation tools
export CC="${TOOLCHAIN}/bin/${ANDROID_ARCH}-linux-android${ANDROID_API}-clang"
export CXX="${TOOLCHAIN}/bin/${ANDROID_ARCH}-linux-android${ANDROID_API}-clang++"
export AR="${TOOLCHAIN}/bin/llvm-ar"
export RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
export STRIP="${TOOLCHAIN}/bin/llvm-strip"

# Build flags
export CFLAGS="-fPIC -O2 -DNDEBUG"
export CXXFLAGS="-fPIC -O2 -DNDEBUG"
export LDFLAGS=""

echo "=== Building MuPDF ${MUPDF_VERSION} for Android ${ANDROID_ABI} ==="
echo "NDK: ${ANDROID_NDK}"
echo "API Level: ${ANDROID_API}"
echo "CC: ${CC}"
echo ""

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Download and extract MuPDF if not present
if [ ! -d "mupdf-${MUPDF_VERSION}-source" ]; then
    echo "=== Downloading MuPDF ${MUPDF_VERSION} ==="
    wget -q --show-progress -O "mupdf-${MUPDF_VERSION}-source.tar.gz" "${MUPDF_URL}"
    
    echo "=== Extracting ==="
    tar xzf "mupdf-${MUPDF_VERSION}-source.tar.gz"
    rm "mupdf-${MUPDF_VERSION}-source.tar.gz"
fi

cd "mupdf-${MUPDF_VERSION}-source"

# Clean previous build
echo "=== Cleaning previous build ==="
make clean 2>/dev/null || true

# Build MuPDF with cross-compilation settings
# MuPDF uses a Makefile-based build system
echo "=== Building MuPDF ==="

# Build the library (not the tools)
# HAVE_X11=no HAVE_GLUT=no - disable GUI dependencies
# HAVE_CURL=no - disable network features (optional)
# USE_SYSTEM_* - use bundled dependencies (simpler for cross-compile)

make \
    HAVE_X11=no \
    HAVE_GLUT=no \
    HAVE_CURL=no \
    HAVE_OBJCOPY=no \
    USE_SYSTEM_FREETYPE=no \
    USE_SYSTEM_HARFBUZZ=no \
    USE_SYSTEM_LIBJPEG=no \
    USE_SYSTEM_ZLIB=no \
    USE_SYSTEM_OPENJPEG=no \
    USE_SYSTEM_JBIG2DEC=no \
    USE_SYSTEM_LCMS2=no \
    USE_SYSTEM_MUJS=no \
    USE_SYSTEM_GUMBO=no \
    USE_SYSTEM_LEPTONICA=no \
    USE_SYSTEM_TESSERACT=no \
    shared=no \
    verbose=yes \
    XCFLAGS="${CFLAGS}" \
    OS=Linux \
    build=release \
    libs \
    -j$(nproc)

echo "=== Installing headers and libraries ==="

# Create output directories
mkdir -p "${BUILD_DIR}/lib"
mkdir -p "${BUILD_DIR}/include/mupdf"

# Copy libraries
cp build/release/libmupdf.a "${BUILD_DIR}/lib/"
cp build/release/libmupdf-third.a "${BUILD_DIR}/lib/"

# Copy headers
cp -r include/mupdf/* "${BUILD_DIR}/include/mupdf/"

echo ""
echo "=== Build Complete ==="
echo "Libraries:"
ls -la "${BUILD_DIR}/lib/"
echo ""
echo "Headers:"
ls "${BUILD_DIR}/include/mupdf/" | head -10
echo ""
echo "To use in CMake:"
echo "  set(MUPDF_INCLUDE_DIR ${BUILD_DIR}/include)"
echo "  set(MUPDF_LIBRARIES ${BUILD_DIR}/lib/libmupdf.a ${BUILD_DIR}/lib/libmupdf-third.a)"

