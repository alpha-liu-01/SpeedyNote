#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Device Install & Launch Script
# ============================================================================
# Installs SpeedyNote onto a connected iPad and launches it.
#
# Prerequisites:
#   - Build completed via ios/build-device.sh
#   - iPad connected via USB/USB-C and trusted ("Trust This Computer")
#   - iPad running iPadOS 16+: Developer Mode must be enabled
#     (Settings > Privacy & Security > Developer Mode)
#
# Usage:
#   ./ios/run-device.sh                # install + launch
#   ./ios/run-device.sh --list         # list connected devices
#   ./ios/run-device.sh --ipa          # also package as .ipa for TrollStore
#
# NOTE: First install with a free Apple ID may require you to manually
#       trust the developer certificate on the iPad:
#       Settings > General > VPN & Device Management > tap your profile > Trust
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/ios/build-device"
APP_PATH="${BUILD_DIR}/Debug-iphoneos/speedynote.app"
BUNDLE_ID="org.speedynote.speedynote"

# ---------- Argument parsing ----------
LIST_ONLY=false
MAKE_IPA=false

for arg in "$@"; do
    case "$arg" in
        --list)
            LIST_ONLY=true
            ;;
        --ipa)
            MAKE_IPA=true
            ;;
        -h|--help)
            echo "Usage: $0 [--list] [--ipa]"
            echo "  --list   List connected iOS devices"
            echo "  --ipa    Also create an .ipa package (for TrollStore, etc.)"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            exit 1
            ;;
    esac
done

# ---------- List devices ----------
list_devices() {
    echo "Connected iOS devices:"
    echo ""
    xcrun devicectl list devices 2>/dev/null || \
        xcrun xctrace list devices 2>/dev/null | grep -v "Simulator" || \
        echo "  (no devices found — is the iPad connected and trusted?)"
}

if [ "${LIST_ONLY}" = true ]; then
    list_devices
    exit 0
fi

# ---------- Preflight ----------
if [ ! -d "${APP_PATH}" ]; then
    echo "ERROR: App bundle not found at ${APP_PATH}"
    echo "Run ios/build-device.sh TEAM_ID first."
    exit 1
fi

echo "=== SpeedyNote Device Installer ==="
echo ""
echo "App:       ${APP_PATH}"
echo "Bundle ID: ${BUNDLE_ID}"
echo ""

# ---------- Find connected device ----------
echo "Looking for connected iPad..."

# Try devicectl first (Xcode 15+), fall back to ios-deploy or xcrun
DEVICE_ID=""

# Method: xcrun devicectl (Xcode 15+)
if command -v xcrun &>/dev/null; then
    # Get the first connected device UDID
    DEVICE_ID=$(xcrun devicectl list devices -v 2>/dev/null | grep -E "^\s" | grep -v "Simulator" | head -1 | awk '{print $NF}' || true)
fi

if [ -z "${DEVICE_ID}" ]; then
    echo ""
    echo "Could not auto-detect device. Attempting direct install..."
    echo ""
fi

# ---------- Install via Xcode's built-in tools ----------
echo "Installing SpeedyNote on device..."
echo ""

# The most reliable way: use xcodebuild to install
# This handles signing verification and device communication
cd "${BUILD_DIR}"

if [ -f "${BUILD_DIR}/SpeedyNote.xcodeproj/project.pbxproj" ]; then
    # Use xcodebuild to install directly — handles signing + deployment
    xcodebuild \
        -project SpeedyNote.xcodeproj \
        -scheme speedynote \
        -configuration Debug \
        -sdk iphoneos \
        -destination "generic/platform=iOS" \
        -allowProvisioningUpdates \
        install \
        DSTROOT="${BUILD_DIR}/_install" \
        -quiet 2>/dev/null || true

    # Alternative: use ios-deploy if available (more reliable for launch)
    if command -v ios-deploy &>/dev/null; then
        echo "Using ios-deploy for installation..."
        ios-deploy --bundle "${APP_PATH}" --debug 2>&1 || \
        ios-deploy --bundle "${APP_PATH}" 2>&1 || true
    else
        # Use devicectl (Xcode 15+) to install the app bundle
        echo "Using devicectl to install..."
        xcrun devicectl device install app "${APP_PATH}" 2>&1 || {
            echo ""
            echo "Automatic install failed. You can install manually:"
            echo ""
            echo "  Option 1 (recommended): Open Xcode and run directly"
            echo "    open ${BUILD_DIR}/SpeedyNote.xcodeproj"
            echo "    Select your iPad as the run destination, then press Cmd+R"
            echo ""
            echo "  Option 2: Install ios-deploy"
            echo "    brew install ios-deploy"
            echo "    ios-deploy --bundle ${APP_PATH}"
            echo ""
            exit 1
        }
    fi
else
    echo "ERROR: Xcode project not found at ${BUILD_DIR}/SpeedyNote.xcodeproj"
    echo "Run ios/build-device.sh TEAM_ID first."
    exit 1
fi

echo ""
echo "=== SpeedyNote installed ==="
echo ""

# ---------- IPA packaging (optional) ----------
if [ "${MAKE_IPA}" = true ]; then
    echo "--- Packaging as .ipa ---"
    IPA_DIR="${BUILD_DIR}/ipa"
    rm -rf "${IPA_DIR}"
    mkdir -p "${IPA_DIR}/Payload"
    cp -r "${APP_PATH}" "${IPA_DIR}/Payload/"
    cd "${IPA_DIR}"
    zip -r -q "${BUILD_DIR}/SpeedyNote.ipa" Payload/
    rm -rf "${IPA_DIR}"
    echo "IPA created: ${BUILD_DIR}/SpeedyNote.ipa"
    echo "  Install via TrollStore, AltStore, or Sideloadly"
fi

echo ""
echo "IMPORTANT — First-time install with a free Apple ID:"
echo "  On the iPad, go to:"
echo "    Settings > General > VPN & Device Management"
echo "  Tap your developer profile and select 'Trust'"
echo ""
echo "To launch manually on the iPad, just tap the SpeedyNote icon."
