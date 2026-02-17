#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Simulator Build Script
# ============================================================================
# Configures and builds SpeedyNote for the iOS Simulator using Qt 6.7.3.
#
# Prerequisites:
#   - Qt 6.7.3 for iOS installed at ~/Qt/6.7.3/ios/
#   - Xcode 15+ with iOS Simulator runtime
#   - MuPDF cross-compiled (run ios/build-mupdf.sh first)
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/build-sim.sh            # configure + build
#   ./ios/build-sim.sh --clean    # wipe build dir, then configure + build
#   ./ios/build-sim.sh --rebuild  # skip configure, just rebuild
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/ios/build-sim"
QT_CMAKE="${HOME}/Qt/6.7.3/ios/bin/qt-cmake"
MUPDF_DIR="${PROJECT_ROOT}/ios/mupdf-build-sim"

# ---------- Argument parsing ----------
CLEAN=false
REBUILD=false

for arg in "$@"; do
    case "$arg" in
        --clean)  CLEAN=true ;;
        --rebuild) REBUILD=true ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--rebuild]"
            echo "  --clean    Remove build directory before configuring"
            echo "  --rebuild  Skip configure step, just rebuild"
            exit 0
            ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

# ---------- Preflight checks ----------
echo "=== SpeedyNote iOS Simulator Build ==="
echo ""

if [ ! -f "${QT_CMAKE}" ]; then
    echo "ERROR: Qt 6.7.3 for iOS not found at ${QT_CMAKE}"
    echo "Install it via aqtinstall or the Qt Online Installer."
    echo "See docs/private/IPADOS_PHASE1_QT_SETUP.md for instructions."
    exit 1
fi

if [ ! -f "${MUPDF_DIR}/lib/libmupdf.a" ]; then
    echo "ERROR: MuPDF (simulator) not found at ${MUPDF_DIR}/lib/libmupdf.a"
    echo "Run: ./ios/build-mupdf.sh --simulator"
    exit 1
fi

if [ ! -f "${PROJECT_ROOT}/ios/Info.plist" ]; then
    echo "ERROR: ios/Info.plist not found."
    exit 1
fi

# ---------- Clean if requested ----------
if [ "${CLEAN}" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

# ---------- Configure (unless --rebuild) ----------
if [ "${REBUILD}" = false ]; then
    echo ""
    echo "--- Configuring with qt-cmake (Xcode generator) ---"
    echo ""
    cd "${BUILD_DIR}"
    "${QT_CMAKE}" -GXcode \
        -DCMAKE_BUILD_TYPE=Debug \
        -DMUPDF_INCLUDE_DIR="${MUPDF_DIR}/include" \
        -DMUPDF_LIBRARIES="${MUPDF_DIR}/lib/libmupdf.a;${MUPDF_DIR}/lib/libmupdf-third.a" \
        "${PROJECT_ROOT}"
fi

# ---------- Build for Simulator ----------
echo ""
echo "--- Building for iOS Simulator ---"
echo ""
cd "${BUILD_DIR}"

# -sdk iphonesimulator tells xcodebuild to target the Simulator
# -allowProvisioningUpdates silenced if no signing identity
cmake --build . --config Debug -- \
    -sdk iphonesimulator \
    -quiet

echo ""
echo "=== Build complete ==="

# Find the .app bundle
APP_PATH="${BUILD_DIR}/Debug-iphonesimulator/speedynote.app"
if [ -d "${APP_PATH}" ]; then
    echo "App bundle: ${APP_PATH}"
    echo ""
    echo "To install and run in Simulator:"
    echo "  ./ios/run-sim.sh"
else
    echo "WARNING: Expected app bundle not found at ${APP_PATH}"
    echo "Check build output above for errors."
    # Try to find it
    find "${BUILD_DIR}" -name "*.app" -type d 2>/dev/null | head -3
fi
