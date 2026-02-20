#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS .deb Packaging Script
# ============================================================================
# Packages the device .app bundle into a rootless .deb for jailbroken iPads.
# The .deb installs to /var/jb/Applications/ (rootless jailbreak standard).
#
# Prerequisites:
#   - A completed ad-hoc device build: ./ios/build-device.sh
#   - dpkg-deb (brew install dpkg)
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/build-deb.sh
#
# Output:
#   ios/dist/SpeedyNote_<version>_iphoneos-arm64.deb
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

DEB_NAME="SpeedyNote_${VERSION}_iphoneos-arm64.deb"

# ---------- Preflight checks ----------
echo "=== SpeedyNote .deb Packaging ==="
echo ""
echo "Version: ${VERSION}"
echo "Output:  ${DIST_DIR}/${DEB_NAME}"
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

if ! command -v dpkg-deb &>/dev/null; then
    echo "ERROR: dpkg-deb not found."
    echo "Install with: brew install dpkg"
    exit 1
fi

# Verify arm64
ARCH_CHECK=$(lipo -info "${APP_PATH}/speedynote" 2>/dev/null || true)
if [[ "${ARCH_CHECK}" != *"arm64"* ]]; then
    echo "ERROR: Binary is not arm64. Got: ${ARCH_CHECK}"
    echo "Make sure you built for device (not simulator)."
    exit 1
fi

# ---------- Build staging directory ----------
STAGING=$(mktemp -d)
trap 'rm -rf "${STAGING}"' EXIT

echo "--- Creating .deb structure ---"

# App payload (rootless path)
APP_DEST="${STAGING}/var/jb/Applications/SpeedyNote.app"
mkdir -p "${APP_DEST}"
cp -R "${APP_PATH}/" "${APP_DEST}/"

# DEBIAN metadata
mkdir -p "${STAGING}/DEBIAN"

cat > "${STAGING}/DEBIAN/control" << EOF
Package: org.speedynote.speedynote
Name: SpeedyNote
Version: ${VERSION}
Architecture: iphoneos-arm64
Description: Stylus-focused note-taking app for iPad with Apple Pencil support.
Maintainer: SpeedyNote <info@speedynote.org>
Section: Productivity
Depends: firmware (>= 16.0)
EOF

cat > "${STAGING}/DEBIAN/postinst" << 'EOF'
#!/bin/bash
uicache -p /var/jb/Applications/SpeedyNote.app
exit 0
EOF
chmod 755 "${STAGING}/DEBIAN/postinst"

cat > "${STAGING}/DEBIAN/postrm" << 'EOF'
#!/bin/bash
uicache -p /var/jb/Applications/SpeedyNote.app
exit 0
EOF
chmod 755 "${STAGING}/DEBIAN/postrm"

# ---------- Build .deb ----------
echo ""
echo "--- Building .deb ---"

mkdir -p "${DIST_DIR}"
dpkg-deb --root-owner-group -Zxz -b "${STAGING}" "${DIST_DIR}/${DEB_NAME}"

DEB_SIZE=$(du -sh "${DIST_DIR}/${DEB_NAME}" | awk '{print $1}')

echo ""
echo "=== Done ==="
echo "Output: ${DIST_DIR}/${DEB_NAME} (${DEB_SIZE})"
echo ""
echo "To install on a jailbroken iPad:"
echo "  scp ${DIST_DIR}/${DEB_NAME} root@<ipad-ip>:/tmp/"
echo "  ssh root@<ipad-ip> 'dpkg -i /tmp/${DEB_NAME}'"
