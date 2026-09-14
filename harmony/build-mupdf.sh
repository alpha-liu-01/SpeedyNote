#!/usr/bin/env bash
# =============================================================================
# Cross-compile MuPDF for HarmonyOS (arm64-v8a)
# =============================================================================
# Builds libmupdf.a and libmupdf-third.a as static libraries against the OHOS
# clang toolchain, for linking into libspeedynote.so.
#
# Prerequisites:
#   - HarmonyOS Command Line Tools at /opt/harmonyos/command-line-tools
#     (see harmony/build-hap.sh for why that path is fixed)
#   - curl
#
# Usage (from the SpeedyNote project root):
#   ./harmony/build-mupdf.sh
#   ./harmony/build-mupdf.sh --clean     # rebuild from scratch
#
# Output:
#   harmony/mupdf-build/lib/libmupdf.a
#   harmony/mupdf-build/lib/libmupdf-third.a
#   harmony/mupdf-build/include/mupdf/*.h
#
# -----------------------------------------------------------------------------
# Why this differs from the other platforms
# -----------------------------------------------------------------------------
# No HarfBuzz surgery. ios/build-mupdf.sh goes to considerable trouble to strip
# MuPDF's bundled HarfBuzz, because on iOS everything links statically into one
# binary alongside Qt's own bundled copy and the two collide. That does not
# apply here: Qt for HarmonyOS ships libQt6Gui.so with its HarfBuzz statically
# linked and hidden, exporting and importing zero hb_* symbols, so MuPDF's copy
# cannot clash with it. Verified with llvm-readelf --dyn-syms on libQt6Gui.so.
#
# Hidden visibility instead. FreeType is the real hazard. Qt imports 44 FT_*
# symbols from the *shared* libfreetype.so that every Qt HAP bundles, and that
# library is loaded before libspeedynote.so. A bundled MuPDF FreeType compiled
# with default visibility would export the same FT_* names from
# libspeedynote.so, and because default-visibility symbols are preemptible even
# within their own DSO, MuPDF's internal FreeType calls could bind to the other
# implementation -- two FreeType builds sharing one set of symbols and each
# other's state. Compiling all of MuPDF with -fvisibility=hidden makes those
# symbols local to the archive, so every call binds to the copy it was built
# against. Nothing outside libspeedynote.so needs fz_* either, so there is no
# downside.
#
# FreeType needs one extra nudge: it annotates its public API with
# __attribute__((visibility("default"))) unconditionally on clang, which
# overrides -fvisibility=hidden. The patch step below neutralises that macro.
# A belt-and-braces alternative, if this ever proves fragile, is to add
# -Wl,--exclude-libs,ALL when linking libspeedynote.so, which hides every symbol
# coming from a static archive regardless of source annotations.
#
# Everything stays bundled (USE_SYSTEM_*=no) as on Android, which keeps the
# libraries self-contained and independent of whatever the HAP happens to ship.
# With hidden visibility that is safe even where the system offers its own copy,
# as it does for zlib.
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/mupdf-build"
SRC_ROOT="${SCRIPT_DIR}/mupdf-src"

# Keep in step with the other platforms' scripts.
MUPDF_VERSION="1.24.10"
MUPDF_URL="https://mupdf.com/downloads/archive/mupdf-${MUPDF_VERSION}-source.tar.gz"
SRC_DIR="${SRC_ROOT}/mupdf-${MUPDF_VERSION}-source"

OHOS_CLT="${OHOS_CLT:-/opt/harmonyos/command-line-tools}"
OHOS_NATIVE="${OHOS_CLT}/sdk/default/openharmony/native"
LLVM_BIN="${OHOS_NATIVE}/llvm/bin"

# ---------- Argument parsing ----------
CLEAN=false
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=true ;;
        -j*)     JOBS="${arg#-j}" ;;
        -h|--help)
            echo "Usage: $0 [--clean] [-jN]"
            echo "  --clean   Remove previous build output and re-extract sources"
            echo "  -jN       Parallel jobs (default: $(sysctl -n hw.ncpu 2>/dev/null || echo 4))"
            exit 0
            ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

fail() { echo "ERROR: $*" >&2; exit 1; }

# ---------- Toolchain ----------
echo "=== Cross-compiling MuPDF ${MUPDF_VERSION} for HarmonyOS arm64-v8a ==="
echo ""

[ -d "${OHOS_NATIVE}" ] || fail "HarmonyOS SDK not found at ${OHOS_NATIVE}"

CC="${LLVM_BIN}/aarch64-unknown-linux-ohos-clang"
CXX="${LLVM_BIN}/aarch64-unknown-linux-ohos-clang++"
AR="${LLVM_BIN}/llvm-ar"
RANLIB="${LLVM_BIN}/llvm-ranlib"

[ -x "${CC}" ] || fail "OHOS clang not found at ${CC}"

echo "CC:      ${CC}"
echo "AR:      ${AR}"
echo "Jobs:    ${JOBS}"
echo ""

# See the header comment: hidden visibility is what makes bundling FreeType
# safe next to the shared libfreetype.so that Qt pulls in at runtime.
#
# These go in via XCFLAGS/XCXXFLAGS rather than CFLAGS/CXXFLAGS because those are
# the hooks MuPDF's Makefile actually honours: it builds CFLAGS itself as
# "CFLAGS += $(XCFLAGS) -Iinclude", and compiles C++ with "$(CFLAGS)
# $(XCXXFLAGS)" -- so an exported CXXFLAGS is ignored outright.
OHOS_CFLAGS="-fPIC -O2 -DNDEBUG -fvisibility=hidden"
OHOS_CXXFLAGS="-fvisibility-inlines-hidden"

if [ "${CLEAN}" = true ]; then
    echo "=== Cleaning ==="
    rm -rf "${BUILD_DIR}" "${SRC_DIR}"
fi

# ---------- Fetch sources ----------
# A private source tree, deliberately not shared with ios/mupdf-src or
# macos/mupdf-src: those get patched in place (HarfBuzz removal, hb-rename.h)
# and reusing them would silently inherit changes this platform does not want.
mkdir -p "${SRC_ROOT}"
if [ ! -d "${SRC_DIR}" ]; then
    echo "=== Downloading MuPDF ${MUPDF_VERSION} ==="
    cd "${SRC_ROOT}"
    curl -fL -o "mupdf-${MUPDF_VERSION}-source.tar.gz" "${MUPDF_URL}"
    echo "=== Extracting ==="
    tar xzf "mupdf-${MUPDF_VERSION}-source.tar.gz"
    rm "mupdf-${MUPDF_VERSION}-source.tar.gz"
else
    echo "=== Using existing source tree at ${SRC_DIR} ==="
fi

cd "${SRC_DIR}"

# ---------- Patch: let -fvisibility=hidden apply to FreeType ----------
# public-macros.h defines FT_PUBLIC_FUNCTION_ATTRIBUTE as
# __attribute__((visibility("default"))) with no opt-out, and FT_EXPORT() stamps
# it onto every public function. Predefining the macro on the command line does
# not help, because the header's own #define is unguarded and wins.
#
# Appending an override after the include guard is enough: FT_EXPORT expands
# FT_PUBLIC_FUNCTION_ATTRIBUTE at each *use* site, not where FT_EXPORT is
# defined, so redefining it later still takes effect everywhere.
FT_MACROS="thirdparty/freetype/include/freetype/config/public-macros.h"
FT_PATCH_MARKER="SPEEDYNOTE_HARMONY_HIDDEN_VISIBILITY"

echo ""
echo "=== Patching FreeType visibility macros ==="
if [ ! -f "${FT_MACROS}" ]; then
    fail "expected ${FT_MACROS} in the MuPDF source tree"
fi
if grep -q "${FT_PATCH_MARKER}" "${FT_MACROS}"; then
    echo "  already patched"
else
    {
        echo ""
        echo "/* ${FT_PATCH_MARKER}: added by harmony/build-mupdf.sh."
        echo "   FreeType forces default visibility on its public API, which would let the"
        echo "   shared libfreetype.so in the HAP preempt this statically linked copy."
        echo "   Clearing the attribute lets -fvisibility=hidden govern instead. */"
        echo "#undef FT_PUBLIC_FUNCTION_ATTRIBUTE"
        echo "#define FT_PUBLIC_FUNCTION_ATTRIBUTE /* empty */"
    } >> "${FT_MACROS}"
    echo "  cleared FT_PUBLIC_FUNCTION_ATTRIBUTE"
fi

# ---------- Build ----------
echo ""
echo "=== Cleaning previous object files ==="
make clean >/dev/null 2>&1 || true

echo ""
echo "=== Compiling ==="

export CC CXX AR RANLIB
export LDFLAGS=""

# OS=Linux: OHOS is a musl-based Linux, but the *host* here is macOS, so MuPDF's
# own uname-based detection would otherwise configure for Darwin.
# HAVE_OBJCOPY=no: MuPDF would otherwise embed its resource files with objcopy
# using the target's object format, which cannot work from a macOS host.
make \
    OS=Linux \
    HAVE_X11=no \
    HAVE_GLUT=no \
    HAVE_CURL=no \
    HAVE_OBJCOPY=no \
    USE_SYSTEM_FREETYPE=no \
    USE_SYSTEM_HARFBUZZ=no \
    USE_SYSTEM_LIBJPEG=no \
    USE_SYSTEM_ZLIB=no \
    USE_SYSTEM_OPENJPEG=no \
    USE_SYSTEM_JBIG2DEC=no \
    USE_SYSTEM_LCMS2=no \
    USE_SYSTEM_MUJS=no \
    USE_SYSTEM_GUMBO=no \
    USE_SYSTEM_LEPTONICA=no \
    USE_SYSTEM_TESSERACT=no \
    shared=no \
    verbose=yes \
    XCFLAGS="${OHOS_CFLAGS}" \
    XCXXFLAGS="${OHOS_CXXFLAGS}" \
    build=release \
    libs \
    -j"${JOBS}"

# ---------- Install ----------
echo ""
echo "=== Installing libraries ==="
LIB_DIR="${BUILD_DIR}/lib"
mkdir -p "${LIB_DIR}"
cp build/release/libmupdf.a "${LIB_DIR}/"
cp build/release/libmupdf-third.a "${LIB_DIR}/"

echo "=== Installing headers ==="
mkdir -p "${BUILD_DIR}/include/mupdf"
cp -r "${SRC_DIR}/include/mupdf/"* "${BUILD_DIR}/include/mupdf/"

# ---------- Verification ----------
echo ""
echo "=== Verification ==="

# Architecture: every member must be aarch64, or the link into libspeedynote.so
# fails late and confusingly.
ARCHS="$("${LLVM_BIN}/llvm-readelf" -h "${LIB_DIR}/libmupdf.a" 2>/dev/null \
    | grep -oE "Machine:[[:space:]]+.*" | sed 's/Machine:[[:space:]]*//' | sort -u | tr '\n' ' ')"
echo "Machine: ${ARCHS:-unknown}"

# Count symbols that are genuinely exported, i.e. GLOBAL binding *and* DEFAULT
# visibility *and* actually defined here. Checking the binding alone is
# misleading: -fvisibility=hidden leaves symbols GLOBAL and only changes the
# visibility field, so `nm --extern-only` still lists every one of them.
exported_count() {
    "${LLVM_BIN}/llvm-readelf" --syms "$1" 2>/dev/null \
        | awk '$5 == "GLOBAL" && $6 == "DEFAULT" && $7 != "UND" { print $8 }' \
        | sort -u | wc -l | tr -d ' '
}

THIRD_EXPORTED="$(exported_count "${LIB_DIR}/libmupdf-third.a")"
MUPDF_EXPORTED="$(exported_count "${LIB_DIR}/libmupdf.a")"
echo "Exported symbols in libmupdf-third.a: ${THIRD_EXPORTED} (want 0)"
echo "Exported symbols in libmupdf.a:       ${MUPDF_EXPORTED} (want 0)"

if [ "${THIRD_EXPORTED}" != "0" ] || [ "${MUPDF_EXPORTED}" != "0" ]; then
    echo ""
    echo "WARNING: some symbols are still exported and can therefore be preempted"
    echo "by the shared libraries in the HAP. Sample:"
    "${LLVM_BIN}/llvm-readelf" --syms "${LIB_DIR}/libmupdf-third.a" "${LIB_DIR}/libmupdf.a" 2>/dev/null \
        | awk '$5 == "GLOBAL" && $6 == "DEFAULT" && $7 != "UND" { print "  " $8 }' \
        | sort -u | head -10
    echo "Consider linking libspeedynote.so with -Wl,--exclude-libs,ALL."
fi

echo ""
echo "Libraries:"
ls -la "${LIB_DIR}/"
echo ""
echo "Headers:"
ls "${BUILD_DIR}/include/mupdf/" | head -10

echo ""
echo "=== Build Complete ==="
echo ""
echo "Directory layout:"
echo "  harmony/mupdf-build/lib/libmupdf.a         (static library)"
echo "  harmony/mupdf-build/lib/libmupdf-third.a   (third-party deps)"
echo "  harmony/mupdf-build/include/mupdf/*.h      (headers)"
