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

# Verify the ad-hoc signature. Unlike TrollStore, which re-signs .ipa payloads
# on install, a .deb install has nothing that fixes up the signature — AMFI
# validates it as-is and SIGKILLs the app at launch if it is stale.
echo "--- Verifying ad-hoc signature ---"
if ! "${SCRIPT_DIR}/verify-signature.sh" "${APP_PATH}/speedynote"; then
    echo ""
    echo "ERROR: refusing to package a binary with a bad signature."
    echo "It would install fine but crash instantly on launch."
    echo "Re-sign it with:"
    echo "  ldid -S${PROJECT_ROOT}/ios/entitlements.plist ${APP_PATH}/speedynote"
    exit 1
fi
echo ""

# ---------- Build staging directory ----------
STAGING=$(mktemp -d)
trap 'rm -rf "${STAGING}"' EXIT

echo "--- Creating .deb structure ---"

# App payload (rootless path). -p keeps the executable bit on the binary.
APP_DEST="${STAGING}/var/jb/Applications/SpeedyNote.app"
mkdir -p "${APP_DEST}"
cp -Rp "${APP_PATH}/" "${APP_DEST}/"
chmod 755 "${APP_DEST}/speedynote"

INSTALLED_SIZE=$(du -sk "${APP_DEST}" | awk '{print $1}')

# DEBIAN metadata
mkdir -p "${STAGING}/DEBIAN"

cat > "${STAGING}/DEBIAN/control" << EOF
Package: org.speedynote.speedynote
Name: SpeedyNote
Version: ${VERSION}
Architecture: iphoneos-arm64
Description: Stylus-focused note-taking app for iPad with Apple Pencil support.
Maintainer: SpeedyNote <info@speedynote.org>
Author: SpeedyNote <info@speedynote.org>
Section: Productivity
Depends: firmware (>= 16.0)
Installed-Size: ${INSTALLED_SIZE}
EOF

# Maintainer scripts use /bin/sh: iOS 16 has no /bin/bash, and the jailbreak's
# bash lives under the rootless prefix, so a #!/bin/bash shebang can fail.
# uicache is likewise resolved rather than assumed to be on PATH.
cat > "${STAGING}/DEBIAN/postinst" << 'EOF'
#!/bin/sh
UICACHE=$(command -v uicache || echo /var/jb/usr/bin/uicache)
[ -x "${UICACHE}" ] && "${UICACHE}" -p /var/jb/Applications/SpeedyNote.app
exit 0
EOF
chmod 755 "${STAGING}/DEBIAN/postinst"

# Only unregister on real removal — on upgrade, postrm runs after the new
# version is already unpacked, so unregistering there would hide the new app.
cat > "${STAGING}/DEBIAN/postrm" << 'EOF'
#!/bin/sh
case "$1" in
    remove|purge)
        UICACHE=$(command -v uicache || echo /var/jb/usr/bin/uicache)
        [ -x "${UICACHE}" ] && "${UICACHE}" -p /var/jb/Applications/SpeedyNote.app
        ;;
esac
exit 0
EOF
chmod 755 "${STAGING}/DEBIAN/postrm"

# ---------- Build .deb ----------
echo ""
echo "--- Building .deb ---"

mkdir -p "${DIST_DIR}"
dpkg-deb --root-owner-group -Zxz -b "${STAGING}" "${DIST_DIR}/${DEB_NAME}"

DEB_SIZE=$(du -sh "${DIST_DIR}/${DEB_NAME}" | awk '{print $1}')

# ---------- Post-build self-check ----------
# Unpack the finished .deb the same way dpkg will on-device and re-verify, so a
# packaging step that corrupts the payload is caught here rather than on the iPad.
echo ""
echo "--- Verifying packaged .deb ---"
CHECK=$(mktemp -d)
dpkg-deb -x "${DIST_DIR}/${DEB_NAME}" "${CHECK}"
UNPACKED="${CHECK}/var/jb/Applications/SpeedyNote.app/speedynote"

if [ ! -f "${UNPACKED}" ]; then
    echo "ERROR: speedynote binary missing from the packaged .deb."
    rm -rf "${CHECK}"
    exit 1
fi

if [ ! -x "${UNPACKED}" ]; then
    echo "ERROR: speedynote in the .deb is not executable."
    rm -rf "${CHECK}"
    exit 1
fi

if ! "${SCRIPT_DIR}/verify-signature.sh" "${UNPACKED}"; then
    echo "ERROR: packaged binary failed signature verification."
    rm -rf "${CHECK}"
    exit 1
fi

if ! cmp -s "${APP_PATH}/speedynote" "${UNPACKED}"; then
    echo "ERROR: packaged binary differs from the built binary."
    rm -rf "${CHECK}"
    exit 1
fi
echo "OK: payload matches the built app bundle byte-for-byte."
rm -rf "${CHECK}"

echo ""
echo "=== Done ==="
echo "Output: ${DIST_DIR}/${DEB_NAME} (${DEB_SIZE})"
echo ""
echo "To install on a jailbroken iPad without an apt server:"
echo "  scp ${DIST_DIR}/${DEB_NAME} mobile@<ipad-ip>:/tmp/"
echo "  ssh mobile@<ipad-ip> 'sudo dpkg -i /tmp/${DEB_NAME}'"
echo ""
echo "Or serve this directory as a throwaway local repo and add it in Sileo:"
echo "  ./ios/serve-repo.sh"
