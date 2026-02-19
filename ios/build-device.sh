#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Device Build Script
# ============================================================================
# Configures and builds SpeedyNote for a real iPad using Qt 6.9.3.
#
# Prerequisites:
#   - Qt 6.9.3 for iOS installed at ~/Qt/6.9.3/ios/
#   - Xcode 15+ with an Apple ID configured (free or paid)
#   - MuPDF cross-compiled for device: run  ios/build-mupdf.sh  (no flags)
#   - Your Development Team ID (see below)
#
# Finding your Team ID:
#   Option A: Xcode > Settings > Accounts > your Apple ID > Team ID column
#   Option B: Run: grep -A1 "iPhone Developer" <(security find-identity -v -p codesigning)
#             The 10-char alphanumeric in parentheses is your Team ID.
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/build-device.sh TEAM_ID             # configure + build
#   ./ios/build-device.sh TEAM_ID --clean     # wipe build dir first
#   ./ios/build-device.sh TEAM_ID --rebuild   # skip configure, just rebuild
#
# Example:
#   ./ios/build-device.sh A1B2C3D4E5
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/ios/build-device"
QT_CMAKE="${HOME}/Qt/6.9.3/ios/bin/qt-cmake"
MUPDF_DIR="${PROJECT_ROOT}/ios/mupdf-build"

# ---------- Argument parsing ----------
TEAM_ID=""
CLEAN=false
REBUILD=false

for arg in "$@"; do
    case "$arg" in
        --clean)   CLEAN=true ;;
        --rebuild) REBUILD=true ;;
        -h|--help)
            echo "Usage: $0 TEAM_ID [--clean] [--rebuild]"
            echo ""
            echo "  TEAM_ID      Your Apple Development Team ID (required)"
            echo "  --clean      Remove build directory before configuring"
            echo "  --rebuild    Skip configure step, just rebuild"
            echo ""
            echo "Find your Team ID:"
            echo "  Xcode > Settings > Accounts > your Apple ID > Team ID"
            exit 0
            ;;
        *)
            if [ -z "${TEAM_ID}" ]; then
                TEAM_ID="$arg"
            else
                echo "Unknown argument: $arg"
                exit 1
            fi
            ;;
    esac
done

if [ -z "${TEAM_ID}" ]; then
    echo "ERROR: Development Team ID is required."
    echo ""
    echo "Usage: $0 TEAM_ID [--clean] [--rebuild]"
    echo ""
    echo "Find your Team ID:"
    echo "  Xcode > Settings > Accounts > select your Apple ID"
    echo "  The Team ID is in the table (10-char alphanumeric, e.g. A1B2C3D4E5)"
    exit 1
fi

# ---------- Preflight checks ----------
echo "=== SpeedyNote iOS Device Build ==="
echo ""
echo "Team ID: ${TEAM_ID}"
echo ""

if [ ! -f "${QT_CMAKE}" ]; then
    echo "ERROR: Qt 6.9.3 for iOS not found at ${QT_CMAKE}"
    echo "Install it via aqtinstall or the Qt Online Installer."
    exit 1
fi

if [ ! -f "${MUPDF_DIR}/lib/libmupdf.a" ]; then
    echo "ERROR: MuPDF (device) not found at ${MUPDF_DIR}/lib/libmupdf.a"
    echo "Run: ./ios/build-mupdf.sh   (without --simulator)"
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
    echo "--- Configuring with qt-cmake (Xcode generator, device) ---"
    echo ""
    cd "${BUILD_DIR}"
    "${QT_CMAKE}" -GXcode \
        -DCMAKE_BUILD_TYPE=Debug \
        -DMUPDF_INCLUDE_DIR="${MUPDF_DIR}/include" \
        -DMUPDF_LIBRARIES="${MUPDF_DIR}/lib/libmupdf.a;${MUPDF_DIR}/lib/libmupdf-third.a" \
        -DDEVELOPMENT_TEAM="${TEAM_ID}" \
        "${PROJECT_ROOT}"
fi

# ---------- Build for device ----------
echo ""
echo "--- Building for iOS Device (arm64) ---"
echo ""
cd "${BUILD_DIR}"

cmake --build . --config Debug -- \
    -sdk iphoneos \
    -allowProvisioningUpdates \
    -quiet

echo ""
echo "=== Build complete ==="

# Find the .app bundle
APP_PATH="${BUILD_DIR}/Debug-iphoneos/speedynote.app"
if [ -d "${APP_PATH}" ]; then
    echo "App bundle: ${APP_PATH}"
    echo ""
    echo "To install on a connected iPad:"
    echo "  ./ios/run-device.sh"
    echo ""
    echo "Or open the Xcode project to deploy directly:"
    echo "  open ${BUILD_DIR}/SpeedyNote.xcodeproj"
else
    echo "WARNING: Expected app bundle not found at ${APP_PATH}"
    echo "Check build output above for errors."
    find "${BUILD_DIR}" -name "*.app" -type d 2>/dev/null | head -3
fi
