#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Device Log Capture
# ============================================================================
# Streams the connected iPad's syslog over USB and highlights the lines that
# explain launch failures: sandbox denials, AMFI/code-signing rejections,
# FrontBoard/watchdog terminations, and the app's own Qt output.
#
# This works on any iPad connected by USB — jailbroken or not — and needs
# nothing installed on the device.
#
# Qt on iOS routes qDebug/qWarning through the Apple unified log when stderr is
# not a terminal, so the app's messages appear here alongside the system's.
#
# Prerequisites:
#   - brew install libimobiledevice
#   - iPad connected by USB and trusted (unlock it and accept the prompt)
#
# Usage:
#   ./ios/debug-device-log.sh              # highlighted, app-relevant lines
#   ./ios/debug-device-log.sh --raw        # everything, unfiltered
#   ./ios/debug-device-log.sh --crash      # pull crash reports instead
#
# The full unfiltered log is always written to ios/dist/device-log-<time>.txt,
# so --raw only changes what is shown on screen, never what is recorded.
#
# Launch the app on the iPad while this is running.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/dist"
MODE="filtered"

for arg in "$@"; do
    case "$arg" in
        --raw)   MODE="raw" ;;
        --crash) MODE="crash" ;;
        -h|--help)
            echo "Usage: $0 [--raw] [--crash]"
            echo ""
            echo "  (no flags)  Stream syslog, showing only app-relevant lines"
            echo "  --raw       Stream syslog unfiltered"
            echo "  --crash     Download crash reports from the device"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            exit 1
            ;;
    esac
done

if ! command -v idevicesyslog &>/dev/null; then
    echo "ERROR: idevicesyslog not found."
    echo "Install with: brew install libimobiledevice"
    exit 1
fi

# ---------- Confirm a device is attached ----------
UDID=$(idevice_id -l 2>/dev/null | head -1 || true)
if [ -z "${UDID}" ]; then
    echo "ERROR: no device detected over USB."
    echo ""
    echo "Check that:"
    echo "  - the iPad is connected with a data-capable cable"
    echo "  - the iPad is unlocked and you accepted the 'Trust This Computer' prompt"
    exit 1
fi

DEVICE_NAME=$(ideviceinfo -u "${UDID}" -k DeviceName 2>/dev/null || echo "unknown")
IOS_VERSION=$(ideviceinfo -u "${UDID}" -k ProductVersion 2>/dev/null || echo "unknown")

echo "=== SpeedyNote device log ==="
echo "Device: ${DEVICE_NAME} (iOS ${IOS_VERSION})"
echo "UDID:   ${UDID}"
echo ""

mkdir -p "${LOG_DIR}"

# ---------- Crash report mode ----------
if [ "${MODE}" = "crash" ]; then
    if ! command -v idevicecrashreport &>/dev/null; then
        echo "ERROR: idevicecrashreport not found (part of libimobiledevice)."
        exit 1
    fi
    CRASH_DIR="${LOG_DIR}/crash-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "${CRASH_DIR}"
    echo "--- Downloading crash reports to ${CRASH_DIR} ---"
    idevicecrashreport -u "${UDID}" -e "${CRASH_DIR}" || true
    echo ""
    echo "--- SpeedyNote-related reports ---"
    find "${CRASH_DIR}" -iname '*speedynote*' -print 2>/dev/null || true
    echo ""
    echo "A launch that is killed for code signing shows Termination Reason"
    echo "CODESIGNING; a hung launch shows 0x8badf00d (watchdog)."
    exit 0
fi

# ---------- Live syslog ----------
LOG_FILE="${LOG_DIR}/device-log-$(date +%Y%m%d-%H%M%S).txt"

echo "Recording full log to: ${LOG_FILE}"
if [ "${MODE}" = "raw" ]; then
    echo "Showing: everything"
else
    echo "Showing: app, code-signing, sandbox and app-lifecycle lines"
fi
echo ""
echo ">>> Now launch SpeedyNote on the iPad. Ctrl-C to stop. <<<"
echo ""

# Highlight rather than discard: a launch problem is often explained by a line
# from kernel/sandboxd/amfid that never mentions the app by name.
idevicesyslog -u "${UDID}" --no-colors 2>&1 \
    | tee "${LOG_FILE}" \
    | awk -v mode="${MODE}" '
    BEGIN {
        red    = "\033[1;31m"
        yellow = "\033[1;33m"
        cyan   = "\033[1;36m"
        reset  = "\033[0m"
    }
    {
        line = $0
        is_app   = (line ~ /[Ss]peedy[Nn]ote/)
        is_deny  = (line ~ /Sandbox:|deny\(|denied|AMFI|amfid|[Cc]ode ?[Ss]ign|CS_|[Tt]rust ?[Cc]ache|Library Validation|mmap|Invalid signature/)
        is_life  = (line ~ /FBSOpenApplication|FrontBoard|SpringBoard|watchdog|0x8badf00d|jetsam|Terminating|terminated|exited abnormally|scene|assertion/)
        is_qt    = (line ~ /qt\.|Qt |QPA|QCoreApplication|QWidget|QPainter|QImage|qml/)

        if (is_deny)        { print red    line reset; fflush(); next }
        if (is_app || is_qt){ print cyan   line reset; fflush(); next }
        if (is_life)        { print yellow line reset; fflush(); next }
        if (mode == "raw")  { print line; fflush() }
    }'
