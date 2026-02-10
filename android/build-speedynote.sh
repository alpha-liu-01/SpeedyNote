#!/bin/bash
# =============================================================================
# Build SpeedyNote for Android
# =============================================================================
# Run inside the Docker container:
#   ./android/build-speedynote.sh [options]
#
# Options:
#   --apk       Build APK only (default)
#   --aab       Build AAB only (for Play Store)
#   --both      Build both APK and AAB
#   --arm64     Build 64-bit only (arm64-v8a)
#   --arm32     Build 32-bit only (armeabi-v7a)
#               (default: both ABIs if neither --arm64 nor --arm32 is given)
#   --release   Use release keystore (requires environment variables)
#
# Release Signing:
#   Set these environment variables for --release:
#     RELEASE_KEYSTORE      Path to release keystore file
#     RELEASE_KEY_ALIAS     Key alias in the keystore
#     RELEASE_STORE_PASS    Keystore password
#     RELEASE_KEY_PASS      Key password (optional, defaults to RELEASE_STORE_PASS)
#
#   Example:
#     export RELEASE_KEYSTORE=/path/to/release.keystore
#     export RELEASE_KEY_ALIAS=speedynote
#     export RELEASE_STORE_PASS=your_secure_password
#     ./android/build-speedynote.sh --aab --release
#
# Prerequisites:
#   - MuPDF built (run ./android/build-mupdf.sh first)
#   - Docker container running (./android/docker-shell.sh)
#
# Output:
#   android/SpeedyNote.apk  (if --apk or --both)
#   android/SpeedyNote.aab  (if --aab or --both)
#
# =============================================================================
set -e

# =============================================================================
# Parse command line arguments
# =============================================================================
BUILD_APK=false
BUILD_AAB=false
BUILD_ARM64=true
BUILD_ARM32=true
USE_RELEASE_SIGNING=false
ENABLE_DEBUG_LOGGING=false

while [ $# -gt 0 ]; do
    case "$1" in
        --apk)
            BUILD_APK=true
            shift
            ;;
        --aab)
            BUILD_AAB=true
            shift
            ;;
        --both)
            BUILD_APK=true
            BUILD_AAB=true
            shift
            ;;
        --arm64)
            BUILD_ARM64=true
            BUILD_ARM32=false
            shift
            ;;
        --arm32)
            BUILD_ARM64=false
            BUILD_ARM32=true
            shift
            ;;
        --release)
            USE_RELEASE_SIGNING=true
            shift
            ;;
        --debug)
            ENABLE_DEBUG_LOGGING=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--apk|--aab|--both] [--arm64|--arm32] [--release] [--debug]"
            echo ""
            echo "Output options:"
            echo "  --apk       Build APK only (default)"
            echo "  --aab       Build AAB only (for Play Store)"
            echo "  --both      Build both APK and AAB"
            echo ""
            echo "Architecture options:"
            echo "  --arm64     Build 64-bit only (arm64-v8a)"
            echo "  --arm32     Build 32-bit only (armeabi-v7a)"
            echo "              Default: both ABIs"
            echo ""
            echo "Signing options:"
            echo "  --release   Use release keystore instead of debug"
            echo ""
            echo "Debug options:"
            echo "  --debug     Enable debug logging (SPEEDYNOTE_DEBUG)"
            echo "              View logs with: adb logcat -s Qt:D 2>&1 | grep -i speedynote"
            echo ""
            echo "Release signing environment variables:"
            echo "  RELEASE_KEYSTORE    Path to release keystore file (required)"
            echo "  RELEASE_KEY_ALIAS   Key alias in the keystore (required)"
            echo "  RELEASE_STORE_PASS  Keystore password (required)"
            echo "  RELEASE_KEY_PASS    Key password (optional, defaults to RELEASE_STORE_PASS)"
            echo ""
            echo "Examples:"
            echo "  # Quick 64-bit debug build for testing"
            echo "  $0 --arm64 --debug"
            echo ""
            echo "  # 32-bit only APK for legacy devices"
            echo "  $0 --arm32 --apk"
            echo ""
            echo "  # Full multi-ABI release for Play Store"
            echo "  export RELEASE_KEYSTORE=/path/to/release.keystore"
            echo "  export RELEASE_KEY_ALIAS=speedynote"
            echo "  export RELEASE_STORE_PASS=your_password"
            echo "  $0 --aab --release"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information."
            exit 1
            ;;
    esac
done

# Default: APK if no output format specified
if [ "$BUILD_APK" = false ] && [ "$BUILD_AAB" = false ]; then
    BUILD_APK=true
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${SCRIPT_DIR}/build-app"

# Qt and Android paths
QT_ANDROID="${QT_ANDROID:-/opt/qt/6.9.3/android_arm64_v8a}"
QT_ANDROID_ARMV7="${QT_ANDROID_ARMV7:-/opt/qt/6.9.3/android_armv7}"
QT_HOST="${QT_HOST:-/opt/qt/6.9.3/gcc_64}"
ANDROID_SDK="${ANDROID_SDK_ROOT:-/opt/android-sdk}"
ANDROID_NDK="${ANDROID_NDK_ROOT:-/opt/android-sdk/ndk/27.2.12479018}"

# MuPDF paths (per-ABI libraries, shared headers)
MUPDF_INCLUDE_DIR="${SCRIPT_DIR}/mupdf-build/include"
MUPDF_ARM64_LIB="${SCRIPT_DIR}/mupdf-build/arm64-v8a/lib"
MUPDF_ARMV7_LIB="${SCRIPT_DIR}/mupdf-build/armeabi-v7a/lib"

# Describe selected ABIs
if [ "$BUILD_ARM64" = true ] && [ "$BUILD_ARM32" = true ]; then
    ABI_DESC="arm64-v8a + armeabi-v7a (both)"
elif [ "$BUILD_ARM64" = true ]; then
    ABI_DESC="arm64-v8a (64-bit only)"
else
    ABI_DESC="armeabi-v7a (32-bit only)"
fi

echo "=== Building SpeedyNote for Android ==="
echo "Workspace: ${WORKSPACE_DIR}"
echo "ABIs: ${ABI_DESC}"
echo "Qt Android arm64: ${QT_ANDROID}"
echo "Qt Android armv7: ${QT_ANDROID_ARMV7}"
echo "Qt Host: ${QT_HOST}"
echo "Build APK: ${BUILD_APK}"
echo "Build AAB: ${BUILD_AAB}"
echo "Release signing: ${USE_RELEASE_SIGNING}"
echo ""

# Check MuPDF is built for selected ABIs
if [ "$BUILD_ARM64" = true ] && [ ! -f "${MUPDF_ARM64_LIB}/libmupdf.a" ]; then
    echo "ERROR: MuPDF for arm64-v8a not found at ${MUPDF_ARM64_LIB}"
    echo "Run ./android/build-mupdf.sh first."
    exit 1
fi
if [ "$BUILD_ARM32" = true ] && [ ! -f "${MUPDF_ARMV7_LIB}/libmupdf.a" ]; then
    echo "ERROR: MuPDF for armeabi-v7a not found at ${MUPDF_ARMV7_LIB}"
    echo "Run ./android/build-mupdf.sh first."
    exit 1
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Clean previous config
rm -rf CMakeCache.txt CMakeFiles

echo "=== Configuring with CMake ==="

# Build CMake args
CMAKE_EXTRA_ARGS=""
if [ "$ENABLE_DEBUG_LOGGING" = true ]; then
    echo "  Debug logging: ENABLED"
    CMAKE_EXTRA_ARGS="-DENABLE_DEBUG_OUTPUT=ON"
fi

# Determine toolchain and ABI list based on architecture selection.
# The CMAKE_TOOLCHAIN_FILE sets the primary ABI; additional ABIs are sub-builds.
# MuPDF library paths are resolved per-ABI in CMakeLists.txt using ${ANDROID_ABI}.
if [ "$BUILD_ARM64" = true ] && [ "$BUILD_ARM32" = true ]; then
    CMAKE_TOOLCHAIN="${QT_ANDROID}/lib/cmake/Qt6/qt.toolchain.cmake"
    ANDROID_ABIS="arm64-v8a;armeabi-v7a"
    ABI_CMAKE_ARGS="-DQT_PATH_ANDROID_ABI_armeabi-v7a:PATH=${QT_ANDROID_ARMV7}"
elif [ "$BUILD_ARM64" = true ]; then
    CMAKE_TOOLCHAIN="${QT_ANDROID}/lib/cmake/Qt6/qt.toolchain.cmake"
    ANDROID_ABIS="arm64-v8a"
    ABI_CMAKE_ARGS=""
else
    CMAKE_TOOLCHAIN="${QT_ANDROID_ARMV7}/lib/cmake/Qt6/qt.toolchain.cmake"
    ANDROID_ABIS="armeabi-v7a"
    ABI_CMAKE_ARGS=""
fi

# Note: NDK r27 supports API 35 natively
cmake \
    -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_SDK_ROOT:PATH="${ANDROID_SDK}" \
    -DANDROID_NDK:PATH="${ANDROID_NDK}" \
    -DANDROID_PLATFORM=android-35 \
    -DQT_ANDROID_TARGET_SDK_VERSION=35 \
    -DQT_HOST_PATH:PATH="${QT_HOST}" \
    -DQT_HOST_PATH_CMAKE_DIR:PATH="${QT_HOST}/lib/cmake" \
    -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
    -DQT_ANDROID_ABIS="${ANDROID_ABIS}" \
    ${ABI_CMAKE_ARGS} \
    -DMUPDF_INCLUDE_DIR:PATH="${MUPDF_INCLUDE_DIR}" \
    -DENABLE_CONTROLLER_SUPPORT=OFF \
    ${CMAKE_EXTRA_ARGS} \
    -G Ninja \
    "${WORKSPACE_DIR}"

echo ""
echo "=== Building ==="
cmake --build . --parallel

# =============================================================================
# Configure signing keystore
# =============================================================================
if [ "$USE_RELEASE_SIGNING" = true ]; then
    echo ""
    echo "=== Using release keystore ==="
    
    # Validate required environment variables
    if [ -z "$RELEASE_KEYSTORE" ]; then
        echo "ERROR: RELEASE_KEYSTORE environment variable not set"
        echo "Set the path to your release keystore file."
        exit 1
    fi
    if [ ! -f "$RELEASE_KEYSTORE" ]; then
        echo "ERROR: Release keystore not found: $RELEASE_KEYSTORE"
        exit 1
    fi
    if [ -z "$RELEASE_KEY_ALIAS" ]; then
        echo "ERROR: RELEASE_KEY_ALIAS environment variable not set"
        exit 1
    fi
    if [ -z "$RELEASE_STORE_PASS" ]; then
        echo "ERROR: RELEASE_STORE_PASS environment variable not set"
        exit 1
    fi
    
    SIGN_KEYSTORE="$RELEASE_KEYSTORE"
    SIGN_KEY_ALIAS="$RELEASE_KEY_ALIAS"
    SIGN_STORE_PASS="$RELEASE_STORE_PASS"
    SIGN_KEY_PASS="${RELEASE_KEY_PASS:-$RELEASE_STORE_PASS}"
    
    echo "Keystore: ${SIGN_KEYSTORE}"
    echo "Key alias: ${SIGN_KEY_ALIAS}"
else
    echo ""
    echo "=== Using debug keystore ==="
    
    # Generate debug keystore if needed
    DEBUG_KEYSTORE="${SCRIPT_DIR}/debug.keystore"
    if [ ! -f "${DEBUG_KEYSTORE}" ]; then
        echo "Generating debug keystore..."
        keytool -genkey -v \
            -keystore "${DEBUG_KEYSTORE}" \
            -alias androiddebugkey \
            -keyalg RSA \
            -keysize 2048 \
            -validity 10000 \
            -storepass android \
            -keypass android \
            -dname "CN=Android Debug,O=Android,C=US"
    fi
    
    SIGN_KEYSTORE="${DEBUG_KEYSTORE}"
    SIGN_KEY_ALIAS="androiddebugkey"
    SIGN_STORE_PASS="android"
    SIGN_KEY_PASS="android"
fi

# Find the deployment settings file
DEPLOY_SETTINGS=$(find "${BUILD_DIR}" -name "android-*-deployment-settings.json" | head -1)
if [ -z "$DEPLOY_SETTINGS" ]; then
    echo "ERROR: Could not find deployment settings file"
    ls -la "${BUILD_DIR}"
    exit 1
fi
echo "Using deployment settings: ${DEPLOY_SETTINGS}"

# =============================================================================
# Build APK (if requested)
# =============================================================================
if [ "$BUILD_APK" = true ]; then
    echo ""
    echo "=== Creating APK ==="
    
    # Create APK with androiddeployqt
    # Note: Qt 6.10+ requires android-35 or higher (androidx.core:core:1.16.0 dependency)
    "${QT_HOST}/bin/androiddeployqt" \
        --input "${DEPLOY_SETTINGS}" \
        --output "${BUILD_DIR}/android-build" \
        --android-platform android-35 \
        --gradle \
        --release \
        --sign "${SIGN_KEYSTORE}" "${SIGN_KEY_ALIAS}" \
        --storepass "${SIGN_STORE_PASS}" \
        --keypass "${SIGN_KEY_PASS}"
    
    # Find and copy APK (specifically look for release signed APK)
    APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk/release" -name "*-signed.apk" 2>/dev/null | head -1)
    if [ -z "$APK_PATH" ]; then
        APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk/release" -name "*.apk" ! -name "*-unsigned.apk" 2>/dev/null | head -1)
    fi
    if [ -z "$APK_PATH" ]; then
        # Fallback: search all directories for release APK
        APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk" -name "*release*-signed.apk" 2>/dev/null | head -1)
    fi
    
    if [ -n "$APK_PATH" ]; then
        echo "APK: ${APK_PATH}"
        ls -lh "${APK_PATH}"
        
        cp "${APK_PATH}" "${SCRIPT_DIR}/SpeedyNote.apk"
        echo "Copied to: ${SCRIPT_DIR}/SpeedyNote.apk"
    else
        echo "WARNING: Could not find APK"
        echo "Check: ${BUILD_DIR}/android-build/build/outputs/apk/"
        ls -laR "${BUILD_DIR}/android-build/build/outputs/apk/" 2>/dev/null || true
    fi
fi

# =============================================================================
# Build AAB (if requested)
# =============================================================================
if [ "$BUILD_AAB" = true ]; then
    echo ""
    echo "=== Creating AAB (Android App Bundle) ==="
    
    # Create AAB with androiddeployqt
    # The --aab flag generates an App Bundle instead of APK
    "${QT_HOST}/bin/androiddeployqt" \
        --input "${DEPLOY_SETTINGS}" \
        --output "${BUILD_DIR}/android-build" \
        --android-platform android-35 \
        --gradle \
        --release \
        --aab \
        --sign "${SIGN_KEYSTORE}" "${SIGN_KEY_ALIAS}" \
        --storepass "${SIGN_STORE_PASS}" \
        --keypass "${SIGN_KEY_PASS}"
    
    # Find and copy AAB (specifically look for release, not debug)
    AAB_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/bundle/release" -name "*.aab" 2>/dev/null | head -1)
    if [ -z "$AAB_PATH" ]; then
        # Fallback: look for any signed AAB
        AAB_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/bundle" -name "*-release*.aab" 2>/dev/null | head -1)
    fi
    
    if [ -n "$AAB_PATH" ]; then
        echo "AAB: ${AAB_PATH}"
        ls -lh "${AAB_PATH}"
        
        cp "${AAB_PATH}" "${SCRIPT_DIR}/SpeedyNote.aab"
        echo "Copied to: ${SCRIPT_DIR}/SpeedyNote.aab"
    else
        echo "WARNING: Could not find AAB"
        echo "Check: ${BUILD_DIR}/android-build/build/outputs/bundle/"
        ls -laR "${BUILD_DIR}/android-build/build/outputs/bundle/" 2>/dev/null || true
    fi
fi

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "=== Build Complete ==="
echo ""

if [ "$USE_RELEASE_SIGNING" = true ]; then
    echo "Signing: RELEASE (${SIGN_KEY_ALIAS})"
else
    echo "Signing: DEBUG (for testing only)"
fi
echo ""

if [ "$BUILD_APK" = true ] && [ -f "${SCRIPT_DIR}/SpeedyNote.apk" ]; then
    echo "APK: ${SCRIPT_DIR}/SpeedyNote.apk"
    echo "  Install: adb install ${SCRIPT_DIR}/SpeedyNote.apk"
fi

if [ "$BUILD_AAB" = true ] && [ -f "${SCRIPT_DIR}/SpeedyNote.aab" ]; then
    echo "AAB: ${SCRIPT_DIR}/SpeedyNote.aab"
    if [ "$USE_RELEASE_SIGNING" = true ]; then
        echo "  Ready for Google Play Console upload"
    else
        echo "  WARNING: Signed with debug key - NOT suitable for Play Store"
        echo "  Use --release flag for Play Store submission"
    fi
fi

