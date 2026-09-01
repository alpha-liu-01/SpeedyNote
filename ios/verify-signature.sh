#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Ad-hoc Signature Verifier
# ============================================================================
# Checks that an ldid fake-signed Mach-O has a signature that is still
# self-consistent with the file on disk.
#
# This matters because a .deb install has nothing that re-signs the binary, so
# AMFI validates whatever signature the build produced and SIGKILLs the process
# at exec if the CDHash does not match. TrollStore re-signs .ipa payloads on
# install, so it silently masks a broken signature — meaning the .ipa can work
# perfectly while the .deb built from the same bundle dies instantly.
#
# Usage:
#   ./ios/verify-signature.sh <path-to-macho>
# ============================================================================

set -euo pipefail

BIN="${1:-}"

if [ -z "${BIN}" ] || [ ! -f "${BIN}" ]; then
    echo "usage: $0 <path-to-macho>"
    exit 2
fi

FAIL=0

# ---------- LC_CODE_SIGNATURE must exist ----------
SIG_LC=$(otool -l "${BIN}" 2>/dev/null | grep -c LC_CODE_SIGNATURE || true)
if [ "${SIG_LC}" -eq 0 ]; then
    echo "FAIL: ${BIN} has no LC_CODE_SIGNATURE (unsigned)."
    echo "      Sign it with: ldid -S<entitlements.plist> ${BIN}"
    exit 1
fi

DATAOFF=$(otool -l "${BIN}" 2>/dev/null \
    | awk '/LC_CODE_SIGNATURE/{f=1} f&&/dataoff/{print $2; exit}')
DATASIZE=$(otool -l "${BIN}" 2>/dev/null \
    | awk '/LC_CODE_SIGNATURE/{f=1} f&&/datasize/{print $2; exit}')
FILESIZE=$(stat -f%z "${BIN}")

# ---------- Signature blob must end at EOF ----------
if [ "$((DATAOFF + DATASIZE))" -ne "${FILESIZE}" ]; then
    echo "FAIL: signature blob does not end at EOF."
    echo "      dataoff+datasize = $((DATAOFF + DATASIZE)), file size = ${FILESIZE}"
    FAIL=1
fi

# ---------- Signed page count must match the actual code limit ----------
# A post-signing mutation (typically `strip`) shrinks the file but leaves the
# CodeDirectory hashing the old, larger layout. Comparing the hash slot count
# against the code limit catches exactly that.
PAGE_SIZE=4096
PAGES_SIGNED=$(codesign -dv "${BIN}" 2>&1 \
    | sed -n 's/.*hashes=\([0-9]*\)+.*/\1/p' | head -1)
PAGES_EXPECTED=$(( (DATAOFF + PAGE_SIZE - 1) / PAGE_SIZE ))

if [ -z "${PAGES_SIGNED}" ]; then
    echo "WARNING: could not read CodeDirectory hash count; skipping page check."
elif [ "${PAGES_SIGNED}" -ne "${PAGES_EXPECTED}" ]; then
    echo "FAIL: signature is stale — it covers a different binary."
    echo "      CodeDirectory hashes ${PAGES_SIGNED} pages, file needs ${PAGES_EXPECTED}."
    echo "      Difference: $(( (PAGES_SIGNED - PAGES_EXPECTED) * PAGE_SIZE )) bytes."
    echo "      Cause: the binary was modified (stripped?) after signing."
    echo "      Fix: strip first, then run ldid."
    FAIL=1
fi

# ---------- Entitlements must be embedded and parse ----------
ENTS=$(ldid -e "${BIN}" 2>/dev/null || true)
if [ -z "${ENTS}" ]; then
    echo "FAIL: no entitlements embedded."
    echo "      A jailbreak-installed app needs at least platform-application."
    FAIL=1
else
    if ! printf '%s' "${ENTS}" | plutil -lint - >/dev/null 2>&1; then
        echo "WARNING: embedded entitlements could not be linted as a plist."
    fi
    if ! printf '%s' "${ENTS}" | grep -q "platform-application"; then
        echo "WARNING: entitlements do not contain platform-application."
    fi

    # A platform-application is judged against the system sandbox policy, which
    # denies GPU user clients unless they are named explicitly. Missing these
    # does not crash the app — it renders nothing at all, which looks like a
    # black screen and is very hard to diagnose from the device.
    for cls in AGXDeviceUserClient IOSurfaceRootUserClient; do
        if ! printf '%s' "${ENTS}" | grep -q "${cls}"; then
            echo "FAIL: entitlements are missing IOKit user client '${cls}'."
            echo "      The app will launch and run but be unable to draw,"
            echo "      appearing as a black screen with no crash."
            echo "      Expect: System Policy: ... deny(1) iokit-open-user-client ${cls}"
            FAIL=1
        fi
    done
fi

# ---------- DER entitlements must be present ----------
# iOS 15+ evaluates the DER-encoded entitlements blob (slot 7). An XML-only
# blob is silently ignored, so the entitlements above would have no effect.
DER_COUNT=$(tail -c "+$((DATAOFF + 1))" "${BIN}" 2>/dev/null \
    | head -c "${DATASIZE}" \
    | xxd -p 2>/dev/null | tr -d '\n' | grep -c 'fade7172' || true)
if [ "${DER_COUNT}" -eq 0 ]; then
    echo "FAIL: signature has no DER entitlements blob (magic 0xfade7172)."
    echo "      iOS 15+ ignores XML-only entitlements, so they will not apply."
    echo "      Use ldid 2.1.5 or newer to emit DER entitlements."
    FAIL=1
fi

if [ "${FAIL}" -ne 0 ]; then
    echo ""
    echo "Signature check FAILED for ${BIN}"
    exit 1
fi

echo "OK: signature is self-consistent (${PAGES_EXPECTED} pages, ${FILESIZE} bytes)."
