#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Device Build Script
# ============================================================================
# Configures and builds SpeedyNote for a real iPad using Qt 6.9.3.
# Supports two modes:
#
#   1. Provisioned (non-jailbroken device):
#      ./ios/build-device.sh TEAM_ID
#      Requires an Apple ID in Xcode. Builds Debug, signed with Apple Development.
#
#   2. Ad-hoc (jailbreak / TrollStore):
#      ./ios/build-device.sh
#      No Apple ID needed. Builds Release, fake-signed with ldid.
#
# Prerequisites:
#   - Qt 6.9.3 for iOS installed at ~/Qt/6.9.3/ios/
#   - Xcode 15+ with command-line tools
#   - MuPDF cross-compiled for device: run  ios/build-mupdf.sh  (no flags)
#   - ldid (brew install ldid) — only for ad-hoc mode
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/build-device.sh                   # ad-hoc Release build (jailbreak)
#   ./ios/build-device.sh --clean           # ad-hoc, wipe build dir first
#   ./ios/build-device.sh TEAM_ID           # provisioned Debug build
#   ./ios/build-device.sh TEAM_ID --clean   # provisioned, wipe build dir first
#   ./ios/build-device.sh TEAM_ID --release # provisioned Release build
#   ./ios/build-device.sh --rebuild         # skip configure, just rebuild
#
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
RELEASE=false

for arg in "$@"; do
    case "$arg" in
        --clean)   CLEAN=true ;;
        --rebuild) REBUILD=true ;;
        --release) RELEASE=true ;;
        -h|--help)
            echo "Usage: $0 [TEAM_ID] [--clean] [--rebuild] [--release]"
            echo ""
            echo "  TEAM_ID      Apple Development Team ID (optional)"
            echo "               Without it: ad-hoc build for jailbreak/TrollStore"
            echo "               With it:    provisioned build for non-jailbroken devices"
            echo "  --clean      Remove build directory before configuring"
            echo "  --rebuild    Skip configure step, just rebuild"
            echo "  --release    Build Release instead of Debug (always on for ad-hoc)"
            echo ""
            echo "Find your Team ID (only needed for non-jailbroken devices):"
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

# Ad-hoc mode always builds Release
if [ -z "${TEAM_ID}" ]; then
    RELEASE=true
fi

if [ "${RELEASE}" = true ]; then
    BUILD_CONFIG="Release"
else
    BUILD_CONFIG="Debug"
fi

# ---------- Preflight checks ----------
echo "=== SpeedyNote iOS Device Build ==="
echo ""

if [ -n "${TEAM_ID}" ]; then
    echo "Mode:    Provisioned (Apple Development)"
    echo "Team ID: ${TEAM_ID}"
else
    echo "Mode:    Ad-hoc (jailbreak / TrollStore)"
fi
echo "Config:  ${BUILD_CONFIG}"
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

if [ -z "${TEAM_ID}" ] && ! command -v ldid &>/dev/null; then
    echo "ERROR: ldid not found (required for ad-hoc signing)."
    echo "Install with: brew install ldid"
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

    CMAKE_ARGS=(
        -GXcode
        "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
        "-DMUPDF_INCLUDE_DIR=${MUPDF_DIR}/include"
        "-DMUPDF_LIBRARIES=${MUPDF_DIR}/lib/libmupdf.a;${MUPDF_DIR}/lib/libmupdf-third.a"
    )

    if [ -n "${TEAM_ID}" ]; then
        CMAKE_ARGS+=("-DDEVELOPMENT_TEAM=${TEAM_ID}")
    fi

    "${QT_CMAKE}" "${CMAKE_ARGS[@]}" "${PROJECT_ROOT}"
fi

# ---------- Build for device ----------
echo ""
echo "--- Building for iOS Device (arm64, ${BUILD_CONFIG}) ---"
echo ""
cd "${BUILD_DIR}"

XCODEBUILD_ARGS=(
    -sdk iphoneos
    -quiet
)

if [ -n "${TEAM_ID}" ]; then
    XCODEBUILD_ARGS+=(-allowProvisioningUpdates)
else
    XCODEBUILD_ARGS+=(
        "CODE_SIGN_IDENTITY=-"
        "CODE_SIGNING_ALLOWED=NO"
    )
fi

cmake --build . --config "${BUILD_CONFIG}" -- "${XCODEBUILD_ARGS[@]}"

# ---------- Post-build ----------
APP_PATH="${BUILD_DIR}/${BUILD_CONFIG}-iphoneos/speedynote.app"

if [ ! -d "${APP_PATH}" ]; then
    echo ""
    echo "WARNING: Expected app bundle not found at ${APP_PATH}"
    echo "Check build output above for errors."
    find "${BUILD_DIR}" -name "*.app" -type d 2>/dev/null | head -3
    exit 1
fi

# Ad-hoc mode: fake-sign with ldid and strip debug symbols
if [ -z "${TEAM_ID}" ]; then
    echo ""
    echo "--- Fake-signing with ldid (with entitlements) ---"
    ENTITLEMENTS="${PROJECT_ROOT}/ios/entitlements.plist"
    if [ -f "${ENTITLEMENTS}" ]; then
        ldid -S"${ENTITLEMENTS}" "${APP_PATH}/speedynote"
        echo "Signed with entitlements: ${ENTITLEMENTS}"
    else
        ldid -S "${APP_PATH}/speedynote"
        echo "WARNING: entitlements.plist not found, signed without entitlements"
    fi
    echo "Signed: ${APP_PATH}/speedynote"

    echo ""
    echo "--- Stripping debug symbols ---"
    strip -x "${APP_PATH}/speedynote"
    echo "Stripped: ${APP_PATH}/speedynote"
fi

echo ""
echo "=== Build complete ==="
echo "App bundle: ${APP_PATH}"
echo ""

if [ -n "${TEAM_ID}" ]; then
    echo "To install on a connected iPad:"
    echo "  ./ios/run-device.sh"
    echo ""
    echo "Or open the Xcode project to deploy directly:"
    echo "  open ${BUILD_DIR}/SpeedyNote.xcodeproj"
else
    echo "To package for distribution:"
    echo "  ./ios/build-deb.sh    # rootless .deb for Sileo"
    echo "  ./ios/build-ipa.sh    # .ipa for TrollStore"
fi
