#!/usr/bin/env bash
# ============================================================================
# SpeedyNote iOS Simulator Run Script
# ============================================================================
# Boots an iPad Simulator, installs SpeedyNote, and launches it.
#
# Prerequisites:
#   - Build completed via ios/build-sim.sh
#   - Xcode with iOS Simulator runtime installed
#
# Usage:
#   ./ios/run-sim.sh                    # auto-pick first available iPad
#   ./ios/run-sim.sh "iPad (A16)"       # specify a simulator by name
#   ./ios/run-sim.sh --list             # list available iPad simulators
#
# NOTE: You must have Simulator.app open to see the GUI.
#       This script will open it automatically.
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/ios/build-sim"
APP_PATH="${BUILD_DIR}/Debug-iphonesimulator/speedynote.app"
BUNDLE_ID="org.speedynote.speedynote"

# ---------- Argument parsing ----------
DEVICE_NAME=""
LIST_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --list)
            LIST_ONLY=true
            ;;
        -h|--help)
            echo "Usage: $0 [--list] [DEVICE_NAME]"
            echo "  --list          List available iPad simulators"
            echo "  DEVICE_NAME     Name of the simulator (e.g. \"iPad (A16)\")"
            echo ""
            echo "If no device name is given, the first available iPad is used."
            exit 0
            ;;
        *)
            DEVICE_NAME="$arg"
            ;;
    esac
done

# ---------- List available iPads ----------
list_ipads() {
    echo "Available iPad Simulators:"
    echo ""
    xcrun simctl list devices available | grep -i "ipad" || echo "  (none found — install an iOS Simulator runtime in Xcode)"
}

if [ "${LIST_ONLY}" = true ]; then
    list_ipads
    exit 0
fi

# ---------- Preflight checks ----------
if [ ! -d "${APP_PATH}" ]; then
    echo "ERROR: App bundle not found at ${APP_PATH}"
    echo "Run ios/build-sim.sh first."
    exit 1
fi

# ---------- Pick a simulator ----------
if [ -z "${DEVICE_NAME}" ]; then
    # Auto-select the first available iPad
    DEVICE_NAME=$(xcrun simctl list devices available | grep -i "ipad" | head -1 | sed 's/^[[:space:]]*//' | sed 's/ (.*$//')
    if [ -z "${DEVICE_NAME}" ]; then
        echo "ERROR: No iPad Simulator found."
        echo "Install an iOS Simulator runtime via Xcode > Settings > Platforms."
        exit 1
    fi
    echo "Auto-selected simulator: ${DEVICE_NAME}"
fi

# ---------- Get device UDID ----------
DEVICE_UDID=$(xcrun simctl list devices available | grep "${DEVICE_NAME}" | head -1 | grep -oE '\(([A-F0-9-]{36})\)' | tr -d '()')

if [ -z "${DEVICE_UDID}" ]; then
    echo "ERROR: Could not find simulator named '${DEVICE_NAME}'"
    echo ""
    list_ipads
    exit 1
fi

echo "=== SpeedyNote iOS Simulator Runner ==="
echo "Device:    ${DEVICE_NAME}"
echo "UDID:      ${DEVICE_UDID}"
echo "App:       ${APP_PATH}"
echo "Bundle ID: ${BUNDLE_ID}"
echo ""

# ---------- Boot the simulator ----------
DEVICE_STATE=$(xcrun simctl list devices | grep "${DEVICE_UDID}" | grep -oE '\(Booted\)' || true)

if [ -z "${DEVICE_STATE}" ]; then
    echo "Booting simulator (this may take 30-60 seconds on first boot)..."
    xcrun simctl boot "${DEVICE_UDID}" 2>/dev/null || true
    # Wait for boot to complete
    sleep 3
else
    echo "Simulator already booted."
fi

# ---------- Open Simulator.app (so you can see the GUI) ----------
echo "Opening Simulator.app..."
open -a Simulator

# Give Simulator.app a moment to come up
sleep 2

# ---------- Install the app ----------
echo "Installing SpeedyNote..."
xcrun simctl install "${DEVICE_UDID}" "${APP_PATH}"

# ---------- Launch the app ----------
echo "Launching SpeedyNote..."
xcrun simctl launch "${DEVICE_UDID}" "${BUNDLE_ID}"

echo ""
echo "=== SpeedyNote is running in the Simulator ==="
echo ""
echo "Tips:"
echo "  - Drawing requires a real Apple Pencil (won't work in Simulator)"
echo "  - Use mouse/trackpad for basic UI navigation and touch gestures"
echo "  - Console output: xcrun simctl spawn ${DEVICE_UDID} log stream --predicate 'processImagePath contains \"speedynote\"'"
echo "  - To terminate: xcrun simctl terminate ${DEVICE_UDID} ${BUNDLE_ID}"
echo "  - To uninstall: xcrun simctl uninstall ${DEVICE_UDID} ${BUNDLE_ID}"
