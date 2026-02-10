#!/bin/bash
# =============================================================================
# Cross-compile MuPDF for Android arm64-v8a and armeabi-v7a
# =============================================================================
# This script builds libmupdf and its dependencies for Android using the NDK.
# Both 64-bit (arm64-v8a) and 32-bit (armeabi-v7a) ABIs are built from the
# same source tree.
#
# Run inside the Docker container:
#   ./android/build-mupdf.sh
#
# Output:
#   android/mupdf-build/arm64-v8a/lib/libmupdf.a
#   android/mupdf-build/arm64-v8a/lib/libmupdf-third.a
#   android/mupdf-build/armeabi-v7a/lib/libmupdf.a
#   android/mupdf-build/armeabi-v7a/lib/libmupdf-third.a
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

# NDK toolchain paths
TOOLCHAIN="${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64"
SYSROOT="${TOOLCHAIN}/sysroot"

# Architecture-independent tools
AR="${TOOLCHAIN}/bin/llvm-ar"
RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
STRIP="${TOOLCHAIN}/bin/llvm-strip"

# =============================================================================
# Build MuPDF for a single ABI
# =============================================================================
# Args: $1 = ABI name (e.g. "arm64-v8a")
#        $2 = compiler triple prefix (e.g. "aarch64-linux-android")
build_mupdf_for_abi() {
    local ABI="$1"
    local COMPILER_PREFIX="$2"

    echo ""
    echo "============================================================"
    echo "  Building MuPDF ${MUPDF_VERSION} for Android ${ABI}"
    echo "============================================================"

    local CC_ABI="${TOOLCHAIN}/bin/${COMPILER_PREFIX}${ANDROID_API}-clang"
    local CXX_ABI="${TOOLCHAIN}/bin/${COMPILER_PREFIX}${ANDROID_API}-clang++"

    echo "CC:  ${CC_ABI}"
    echo "CXX: ${CXX_ABI}"
    echo ""

    # Set cross-compilation environment for this ABI
    export CC="${CC_ABI}"
    export CXX="${CXX_ABI}"
    export AR="${AR}"
    export RANLIB="${RANLIB}"
    export STRIP="${STRIP}"
    export CFLAGS="-fPIC -O2 -DNDEBUG"
    export CXXFLAGS="-fPIC -O2 -DNDEBUG"
    export LDFLAGS=""

    cd "${BUILD_DIR}/mupdf-${MUPDF_VERSION}-source"

    # Clean previous build artifacts
    echo "=== Cleaning previous ${ABI} build ==="
    make clean 2>/dev/null || true

    # Build the library (not the tools)
    echo "=== Building MuPDF for ${ABI} ==="
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

    # Install libraries for this ABI
    local ABI_LIB_DIR="${BUILD_DIR}/${ABI}/lib"
    mkdir -p "${ABI_LIB_DIR}"
    cp build/release/libmupdf.a "${ABI_LIB_DIR}/"
    cp build/release/libmupdf-third.a "${ABI_LIB_DIR}/"

    echo "=== ${ABI} libraries installed to ${ABI_LIB_DIR} ==="
    ls -la "${ABI_LIB_DIR}/"
}

# =============================================================================
# Main
# =============================================================================
echo "=== Building MuPDF ${MUPDF_VERSION} for Android (multi-ABI) ==="
echo "NDK: ${ANDROID_NDK}"
echo "API Level: ${ANDROID_API}"
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

# Build for each ABI
# Compiler triple reference:
#   arm64-v8a   -> aarch64-linux-android
#   armeabi-v7a -> armv7a-linux-androideabi  (note: "androideabi" not "android")
build_mupdf_for_abi "arm64-v8a"    "aarch64-linux-android"
build_mupdf_for_abi "armeabi-v7a"  "armv7a-linux-androideabi"

# Install headers (shared across all ABIs)
echo ""
echo "=== Installing headers ==="
mkdir -p "${BUILD_DIR}/include/mupdf"
cp -r "${BUILD_DIR}/mupdf-${MUPDF_VERSION}-source/include/mupdf/"* "${BUILD_DIR}/include/mupdf/"

echo ""
echo "=== Build Complete ==="
echo ""
echo "Libraries:"
echo "  arm64-v8a:"
ls -la "${BUILD_DIR}/arm64-v8a/lib/"
echo "  armeabi-v7a:"
ls -la "${BUILD_DIR}/armeabi-v7a/lib/"
echo ""
echo "Headers:"
ls "${BUILD_DIR}/include/mupdf/" | head -10
echo ""
echo "Directory layout:"
echo "  mupdf-build/include/mupdf/*.h        (shared headers)"
echo "  mupdf-build/arm64-v8a/lib/*.a         (64-bit libraries)"
echo "  mupdf-build/armeabi-v7a/lib/*.a       (32-bit libraries)"
