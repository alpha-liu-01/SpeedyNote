#!/bin/bash
# =============================================================================
# Build the SpeedyNote Android Test App
# =============================================================================
# Run inside the Docker container:
#   ./android/build-test-app.sh
#
# Output:
#   android/test-app/build/android-build/SpeedyNoteTest.apk
#
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
TEST_APP_DIR="${SCRIPT_DIR}/test-app"
BUILD_DIR="${TEST_APP_DIR}/build"

# Qt and Android paths from Docker environment
QT_ANDROID="${QT_ANDROID:-/opt/qt/6.7.2/android_arm64_v8a}"
# Note: aqtinstall uses "linux_gcc_64" as arch name but installs to "gcc_64"
QT_HOST="${QT_HOST:-/opt/qt/6.7.2/gcc_64}"
ANDROID_SDK="${ANDROID_SDK_ROOT:-/opt/android-sdk}"
ANDROID_NDK="${ANDROID_NDK_ROOT:-/opt/android-sdk/ndk/26.1.10909125}"

echo "=== Building SpeedyNote Android Test App ==="
echo "Qt Android: ${QT_ANDROID}"
echo "Qt Host: ${QT_HOST}"
echo "Android SDK: ${ANDROID_SDK}"
echo "Android NDK: ${ANDROID_NDK}"
echo ""

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo "=== Configuring with CMake ==="

# Clean previous failed config completely
rm -rf CMakeCache.txt CMakeFiles

cmake \
    -DCMAKE_TOOLCHAIN_FILE="${QT_ANDROID}/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_SDK_ROOT:PATH="${ANDROID_SDK}" \
    -DANDROID_NDK:PATH="${ANDROID_NDK}" \
    -DQT_HOST_PATH:PATH="${QT_HOST}" \
    -DQT_HOST_PATH_CMAKE_DIR:PATH="${QT_HOST}/lib/cmake" \
    -DQT_ANDROID_BUILD_ALL_ABIS=OFF \
    -DQT_ANDROID_ABIS="arm64-v8a" \
    -G Ninja \
    "${TEST_APP_DIR}"

# Build
echo ""
echo "=== Building ==="
cmake --build . --parallel

# Create APK
echo ""
echo "=== Creating APK ==="

# Generate debug keystore if it doesn't exist
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

# androiddeployqt creates and signs the APK
"${QT_HOST}/bin/androiddeployqt" \
    --input "${BUILD_DIR}/android-SpeedyNoteTest-deployment-settings.json" \
    --output "${BUILD_DIR}/android-build" \
    --android-platform android-34 \
    --gradle \
    --release \
    --sign "${DEBUG_KEYSTORE}" androiddebugkey \
    --storepass android

echo ""
echo "=== Build Complete ==="

# Find the signed APK (prefer signed over unsigned)
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
    
    # Copy to easier location
    cp "${APK_PATH}" "${SCRIPT_DIR}/SpeedyNoteTest.apk"
    echo ""
    echo "Copied to: ${SCRIPT_DIR}/SpeedyNoteTest.apk"
else
    echo "APK location: ${BUILD_DIR}/android-build/"
    ls -la "${BUILD_DIR}/android-build/"
fi

echo ""
echo "To install on device:"
echo "  adb install ${SCRIPT_DIR}/SpeedyNoteTest.apk"

