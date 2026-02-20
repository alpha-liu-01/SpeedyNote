#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS App Icon Generator
# ============================================================================
# Renders resources/icons/mainicon.svg into the Asset Catalog PNGs required
# by Xcode for iPad.  Uses rsvg-convert (brew install librsvg).
#
# Usage:
#   cd <SpeedyNote root>
#   ./ios/generate-icons.sh
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SVG="${PROJECT_ROOT}/resources/icons/mainicon.svg"
OUTDIR="${SCRIPT_DIR}/Assets.xcassets/AppIcon.appiconset"

if [ ! -f "${SVG}" ]; then
    echo "ERROR: SVG not found at ${SVG}"
    exit 1
fi

if ! command -v rsvg-convert &>/dev/null; then
    echo "ERROR: rsvg-convert not found. Install with: brew install librsvg"
    exit 1
fi

mkdir -p "${OUTDIR}"

# iPad icon sizes: name  pixel-size
ICONS=(
    "icon-20.png        20"
    "icon-20@2x.png     40"
    "icon-29.png        29"
    "icon-29@2x.png     58"
    "icon-40.png        40"
    "icon-40@2x.png     80"
    "icon-76.png        76"
    "icon-76@2x.png    152"
    "icon-83.5@2x.png  167"
    "icon-1024.png    1024"
)

echo "=== Generating iPad app icons from mainicon.svg ==="
for entry in "${ICONS[@]}"; do
    read -r name size <<< "${entry}"
    echo "  ${name}  (${size}x${size})"
    rsvg-convert -w "${size}" -h "${size}" "${SVG}" -o "${OUTDIR}/${name}"
done

echo ""
echo "=== Done — icons written to ${OUTDIR} ==="
