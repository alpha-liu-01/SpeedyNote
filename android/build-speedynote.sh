#!/bin/bash
# =============================================================================
# Build SpeedyNote for Android
# =============================================================================
# Run inside the Docker container:
#   ./android/build-speedynote.sh
#
# Prerequisites:
#   - MuPDF built (run ./android/build-mupdf.sh first)
#   - Docker container running (./android/docker-shell.sh)
#
# Output:
#   android/SpeedyNote.apk
#
# =============================================================================
set -e

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
    -G Ninja \
    "${WORKSPACE_DIR}"

echo ""
echo "=== Building ==="
cmake --build . --parallel

echo ""
echo "=== Creating APK ==="

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

# Find the deployment settings file
DEPLOY_SETTINGS=$(find "${BUILD_DIR}" -name "android-*-deployment-settings.json" | head -1)
if [ -z "$DEPLOY_SETTINGS" ]; then
    echo "ERROR: Could not find deployment settings file"
    ls -la "${BUILD_DIR}"
    exit 1
fi
echo "Using deployment settings: ${DEPLOY_SETTINGS}"

# Create APK with androiddeployqt
# Note: Qt 6.10+ requires android-35 or higher (androidx.core:core:1.16.0 dependency)
"${QT_HOST}/bin/androiddeployqt" \
    --input "${DEPLOY_SETTINGS}" \
    --output "${BUILD_DIR}/android-build" \
    --android-platform android-35 \
    --gradle \
    --release \
    --sign "${DEBUG_KEYSTORE}" androiddebugkey \
    --storepass android

echo ""
echo "=== Build Complete ==="

# Find and copy APK
APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk" -name "*-signed.apk" 2>/dev/null | head -1)
if [ -z "$APK_PATH" ]; then
    APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk" -name "*.apk" ! -name "*-unsigned.apk" 2>/dev/null | head -1)
fi
if [ -z "$APK_PATH" ]; then
    APK_PATH=$(find "${BUILD_DIR}/android-build/build/outputs/apk" -name "*.apk" 2>/dev/null | head -1)
fi

if [ -n "$APK_PATH" ]; then
    echo "APK: ${APK_PATH}"
    ls -lh "${APK_PATH}"
    
    cp "${APK_PATH}" "${SCRIPT_DIR}/SpeedyNote.apk"
    echo ""
    echo "Copied to: ${SCRIPT_DIR}/SpeedyNote.apk"
    echo ""
    echo "To install on device:"
    echo "  adb install ${SCRIPT_DIR}/SpeedyNote.apk"
else
    echo "WARNING: Could not find APK"
    echo "Check: ${BUILD_DIR}/android-build/build/outputs/apk/"
    ls -laR "${BUILD_DIR}/android-build/build/outputs/apk/" 2>/dev/null || true
fi

