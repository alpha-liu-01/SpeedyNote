#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS .ipa Packaging Script
# ============================================================================
# Packages the device .app bundle into an .ipa for TrollStore sideloading.
#
# Prerequisites:
#   - A completed ad-hoc device build: ./ios/build-device.sh
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/build-ipa.sh
#
# Output:
#   ios/dist/SpeedyNote_<version>.ipa
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${SCRIPT_DIR}/dist"
APP_PATH="${SCRIPT_DIR}/build-device/Release-iphoneos/speedynote.app"

# ---------- Extract version from CMakeLists.txt ----------
VERSION=$(grep "project(SpeedyNote VERSION" "${PROJECT_ROOT}/CMakeLists.txt" \
    | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')
if [ -z "${VERSION}" ]; then
    VERSION="0.0.0"
fi

IPA_NAME="SpeedyNote_${VERSION}.ipa"

# ---------- Preflight checks ----------
echo "=== SpeedyNote .ipa Packaging ==="
echo ""
echo "Version: ${VERSION}"
echo "Output:  ${DIST_DIR}/${IPA_NAME}"
echo ""

if [ ! -d "${APP_PATH}" ]; then
    echo "ERROR: App bundle not found at ${APP_PATH}"
    echo "Run the device build first: ./ios/build-device.sh"
    exit 1
fi

if [ ! -f "${APP_PATH}/speedynote" ]; then
    echo "ERROR: speedynote binary not found inside ${APP_PATH}"
    exit 1
fi

# Verify arm64
ARCH_CHECK=$(lipo -info "${APP_PATH}/speedynote" 2>/dev/null || true)
if [[ "${ARCH_CHECK}" != *"arm64"* ]]; then
    echo "ERROR: Binary is not arm64. Got: ${ARCH_CHECK}"
    echo "Make sure you built for device (not simulator)."
    exit 1
fi

# ---------- Build .ipa ----------
PAYLOAD=$(mktemp -d)
trap 'rm -rf "${PAYLOAD}"' EXIT

echo "--- Creating Payload structure ---"
mkdir -p "${PAYLOAD}/Payload"
cp -R "${APP_PATH}" "${PAYLOAD}/Payload/SpeedyNote.app"

echo "--- Zipping into .ipa ---"
mkdir -p "${DIST_DIR}"
cd "${PAYLOAD}"
zip -qr "${DIST_DIR}/${IPA_NAME}" Payload/

IPA_SIZE=$(du -sh "${DIST_DIR}/${IPA_NAME}" | awk '{print $1}')

echo ""
echo "=== Done ==="
echo "Output: ${DIST_DIR}/${IPA_NAME} (${IPA_SIZE})"
echo ""
echo "To install via TrollStore:"
echo "  AirDrop or transfer ${IPA_NAME} to the iPad, then open with TrollStore."
