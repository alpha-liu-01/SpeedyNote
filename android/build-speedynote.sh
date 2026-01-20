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
USE_RELEASE_SIGNING=false
ENABLE_DEBUG_LOGGING=false

# Default: APK only (backward compatible)
if [ $# -eq 0 ]; then
    BUILD_APK=true
fi

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
        --release)
            USE_RELEASE_SIGNING=true
            shift
            ;;
        --debug)
            ENABLE_DEBUG_LOGGING=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--apk|--aab|--both] [--release] [--debug]"
            echo ""
            echo "Output options:"
            echo "  --apk       Build APK only (default)"
            echo "  --aab       Build AAB only (for Play Store)"
            echo "  --both      Build both APK and AAB"
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
            echo "  # Debug build with logging (for development)"
            echo "  $0 --apk --debug"
            echo ""
            echo "  # Release build for Play Store"
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

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${SCRIPT_DIR}/build-app"

# Qt and Android paths
QT_ANDROID="${QT_ANDROID:-/opt/qt/6.9.3/android_arm64_v8a}"
QT_HOST="${QT_HOST:-/opt/qt/6.9.3/gcc_64}"
ANDROID_SDK="${ANDROID_SDK_ROOT:-/opt/android-sdk}"
ANDROID_NDK="${ANDROID_NDK_ROOT:-/opt/android-sdk/ndk/27.2.12479018}"

# MuPDF paths
MUPDF_INCLUDE_DIR="${SCRIPT_DIR}/mupdf-build/include"
MUPDF_LIB_DIR="${SCRIPT_DIR}/mupdf-build/lib"

echo "=== Building SpeedyNote for Android ==="
echo "Workspace: ${WORKSPACE_DIR}"
echo "Qt Android: ${QT_ANDROID}"
echo "Qt Host: ${QT_HOST}"
echo "MuPDF: ${MUPDF_LIB_DIR}"
echo "Build APK: ${BUILD_APK}"
echo "Build AAB: ${BUILD_AAB}"
echo "Release signing: ${USE_RELEASE_SIGNING}"
echo ""

# Check MuPDF is built
if [ ! -f "${MUPDF_LIB_DIR}/libmupdf.a" ]; then
    echo "ERROR: MuPDF not found. Run ./android/build-mupdf.sh first."
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

# Note: NDK r27 supports API 35 natively
cmake \
    -DCMAKE_TOOLCHAIN_FILE="${QT_ANDROID}/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_SDK_ROOT:PATH="${ANDROID_SDK}" \
    -DANDROID_NDK:PATH="${ANDROID_NDK}" \
    -DANDROID_PLATFORM=android-35 \
    -DQT_ANDROID_TARGET_SDK_VERSION=35 \
    -DQT_HOST_PATH:PATH="${QT_HOST}" \
    -DQT_HOST_PATH_CMAKE_DIR:PATH="${QT_HOST}/lib/cmake" \
    -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
    -DQT_ANDROID_ABIS="arm64-v8a" \
    -DMUPDF_INCLUDE_DIR:PATH="${MUPDF_INCLUDE_DIR}" \
    -DMUPDF_LIBRARIES:STRING="${MUPDF_LIB_DIR}/libmupdf.a;${MUPDF_LIB_DIR}/libmupdf-third.a" \
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

