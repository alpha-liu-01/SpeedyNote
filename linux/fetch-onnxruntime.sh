#!/bin/bash
# =============================================================================
# Fetch a pinned ONNX Runtime (CPU execution provider) for the Linux OCR backend
# =============================================================================
# Downloads the official prebuilt ONNX Runtime release tarball from
# microsoft/onnxruntime on GitHub and lays it out at:
#
#   linux/onnxruntime-build/include/   (onnxruntime_cxx_api.h, ...)
#   linux/onnxruntime-build/lib/       (libonnxruntime.so*)
#
# This is the find/link root CMake uses to enable SPEEDYNOTE_ENABLE_PADDLE_OCR
# (see CMakeLists.txt). OCR Phase 4B (PaddleOCR via ONNX Runtime). CPU EP only
# (QA Q11.3) -- GPU EPs are vendor-specific and not bundled.
#
# Usage (from the SpeedyNote project root):
#   ./linux/fetch-onnxruntime.sh            # auto-detects the host arch
#   ORT_ARCH=aarch64 ./linux/fetch-onnxruntime.sh   # force ARM64
#   ORT_ARCH=x64     ./linux/fetch-onnxruntime.sh   # force x86_64
#
# To refresh: delete linux/onnxruntime-build/ and re-run.
# =============================================================================
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/onnxruntime-build"

# --- Pinned version ---------------------------------------------------------
ORT_VERSION="1.20.1"

# Default to the host architecture so a bare `./linux/fetch-onnxruntime.sh`
# does the right thing on both x86_64 and ARM64. Override with ORT_ARCH.
# (A hardcoded x64 default previously vendored an x86_64 .so on ARM boxes,
#  which then failed at link time with "file in wrong format".)
_host_ort_arch() {
    case "$(uname -m)" in
        x86_64|amd64)  echo "x64" ;;
        aarch64|arm64) echo "aarch64" ;;
        *)             echo "x64" ;;
    esac
}
ORT_ARCH="${ORT_ARCH:-$(_host_ort_arch)}"   # x64 | aarch64

# Integrity pin (SHA256 of the release .tgz). Microsoft does not publish a
# per-asset SHA256 in a machine-readable form, so these must be filled in by
# hand once per ORT version. To obtain them, run this script once (it prints
# the computed digest after download), then paste the value for each arch
# below and re-run; CI will then verify integrity instead of warning.
#
#   curl -fSL -o ort.tgz \
#     https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz
#   sha256sum ort.tgz
#
# A value may also be supplied out-of-band via the ORT_SHA256 environment
# variable (takes precedence over the constants below), which lets a release
# pipeline pin the digest without editing this file.
ORT_SHA256_x64="67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105"
ORT_SHA256_aarch64="ae4fedbdc8c18d688c01306b4b50c63de3445cdf2dbd720e01a2fa3810b8106a"

case "${ORT_ARCH}" in
    x64)     ORT_SHA256="${ORT_SHA256:-${ORT_SHA256_x64}}" ;;
    aarch64) ORT_SHA256="${ORT_SHA256:-${ORT_SHA256_aarch64}}" ;;
    *) echo -e "${RED}Unsupported ORT_ARCH='${ORT_ARCH}' (use x64 or aarch64)${NC}"; exit 1 ;;
esac

PKG="onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${PKG}.tgz"

# --- Skip if already present ------------------------------------------------
if [ -d "${OUTPUT_DIR}/lib" ] && ls "${OUTPUT_DIR}/lib"/libonnxruntime.so* >/dev/null 2>&1 \
   && [ -f "${OUTPUT_DIR}/include/onnxruntime_cxx_api.h" ]; then
    echo -e "${YELLOW}ONNX Runtime already present at ${OUTPUT_DIR}${NC}"
    echo "Delete linux/onnxruntime-build/ and re-run to refresh."
    exit 0
fi

command -v curl >/dev/null 2>&1 || { echo -e "${RED}Error: curl not found.${NC}"; exit 1; }
command -v tar  >/dev/null 2>&1 || { echo -e "${RED}Error: tar not found.${NC}";  exit 1; }

echo -e "${CYAN}Fetching ${PKG} ...${NC}"
echo "  ${URL}"

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT

TARBALL="${TEMP_DIR}/${PKG}.tgz"
curl -fSL --retry 3 -o "${TARBALL}" "${URL}"

# --- Integrity check --------------------------------------------------------
COMPUTED="$(sha256sum "${TARBALL}" | awk '{print $1}')"
echo -e "${CYAN}SHA256: ${COMPUTED}${NC}"
if [ -n "${ORT_SHA256}" ]; then
    if [ "${COMPUTED}" != "${ORT_SHA256}" ]; then
        echo -e "${RED}Error: SHA256 mismatch!${NC}"
        echo "  expected ${ORT_SHA256}"
        echo "  got      ${COMPUTED}"
        exit 1
    fi
    echo -e "${GREEN}SHA256 verified.${NC}"
else
    echo -e "${YELLOW}Warning: no pinned SHA256 for ${ORT_ARCH}; integrity NOT verified.${NC}"
    echo "  Paste the value above into ORT_SHA256_${ORT_ARCH} in this script to pin it."
fi

# --- Extract + lay out ------------------------------------------------------
tar -xzf "${TARBALL}" -C "${TEMP_DIR}"
EXTRACTED="${TEMP_DIR}/${PKG}"
if [ ! -d "${EXTRACTED}/include" ] || [ ! -d "${EXTRACTED}/lib" ]; then
    echo -e "${RED}Error: unexpected archive layout under ${EXTRACTED}${NC}"
    exit 1
fi

rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"
cp -R "${EXTRACTED}/include" "${OUTPUT_DIR}/include"
cp -R "${EXTRACTED}/lib"     "${OUTPUT_DIR}/lib"

echo ""
echo -e "${GREEN}Done! ONNX Runtime ${ORT_VERSION} (${ORT_ARCH}, CPU) vendored at:${NC}"
echo "  ${OUTPUT_DIR}"
ls -1 "${OUTPUT_DIR}/lib"/libonnxruntime.so* 2>/dev/null | while read -r f; do echo "  $(basename "$f")"; done
echo ""
echo -e "${GREEN}Next: ./linux/fetch-ocr-models.sh, then reconfigure CMake.${NC}"
