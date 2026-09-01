#!/usr/bin/env bash
# ============================================================================
# SpeedyNote Throwaway Local APT Repo
# ============================================================================
# Generates a minimal APT repo index over ios/dist/*.deb and serves it over
# HTTP on the LAN, so the full Sileo download-and-install path can be tested
# without publishing anything to the production apt server.
#
# On the iPad: Sileo > Sources > + > http://<mac-ip>:<port>/
# Remove the source again when you are done.
#
# Prerequisites:
#   - dpkg-deb (brew install dpkg)
#   - A built .deb: ./ios/build-deb.sh
#
# Usage:
#   ./ios/serve-repo.sh [PORT]     # default port 8080
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DIST_DIR="${SCRIPT_DIR}/dist"
PORT="${1:-8080}"

if ! command -v dpkg-deb &>/dev/null; then
    echo "ERROR: dpkg-deb not found. Install with: brew install dpkg"
    exit 1
fi

shopt -s nullglob
DEBS=("${DIST_DIR}"/*.deb)
shopt -u nullglob

if [ ${#DEBS[@]} -eq 0 ]; then
    echo "ERROR: no .deb found in ${DIST_DIR}"
    echo "Build one first: ./ios/build-deb.sh"
    exit 1
fi

# ---------- Generate Packages index ----------
echo "=== SpeedyNote local test repo ==="
echo ""
echo "--- Indexing ${#DEBS[@]} package(s) ---"

PACKAGES="${DIST_DIR}/Packages"
: > "${PACKAGES}"

for deb in "${DEBS[@]}"; do
    name=$(basename "${deb}")
    size=$(stat -f%z "${deb}")
    md5=$(md5 -q "${deb}")
    sha256=$(shasum -a 256 "${deb}" | awk '{print $1}')

    # Control fields, minus any the index must define itself.
    dpkg-deb -f "${deb}" \
        | grep -v -i -E '^(Filename|Size|MD5sum|SHA1|SHA256):' >> "${PACKAGES}"
    {
        echo "Filename: ${name}"
        echo "Size: ${size}"
        echo "MD5sum: ${md5}"
        echo "SHA256: ${sha256}"
        echo ""
    } >> "${PACKAGES}"

    echo "  ${name} (${size} bytes)"
done

gzip -9 -c "${PACKAGES}" > "${PACKAGES}.gz"

cat > "${DIST_DIR}/Release" << 'EOF'
Origin: SpeedyNote Local Test
Label: SpeedyNote Local Test
Suite: stable
Version: 1.0
Codename: ios
Architectures: iphoneos-arm64
Components: main
Description: Throwaway local repo for testing SpeedyNote .deb builds.
EOF

# ---------- Determine LAN address ----------
IP=""
for iface in en0 en1 en2 bridge100; do
    IP=$(ipconfig getifaddr "${iface}" 2>/dev/null || true)
    [ -n "${IP}" ] && break
done
[ -z "${IP}" ] && IP="<mac-ip>"

echo ""
echo "--- Serving ${DIST_DIR} on port ${PORT} ---"
echo ""
echo "Add this source in Sileo on the iPad:"
echo "  http://${IP}:${PORT}/"
echo ""
echo "Then: refresh sources, install SpeedyNote, and launch it."
echo "Remove the source afterwards. Ctrl-C here to stop the server."
echo ""

cd "${DIST_DIR}"
exec python3 -m http.server "${PORT}" --bind 0.0.0.0
