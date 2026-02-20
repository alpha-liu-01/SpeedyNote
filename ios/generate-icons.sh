#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS App Icon Generator
# ============================================================================
# Renders resources/icons/mainicon.svg into the single 1024x1024 PNG used by
# the Asset Catalog.  Modern iOS (14+) auto-generates all other sizes from
# this single universal icon.  Uses rsvg-convert (brew install librsvg).
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

echo "=== Generating iPad app icon from mainicon.svg ==="
echo "  icon-1024.png  (1024x1024)"
rsvg-convert -w 1024 -h 1024 "${SVG}" -o "${OUTDIR}/icon-1024.png"

echo ""
echo "=== Done — icon written to ${OUTDIR} ==="
