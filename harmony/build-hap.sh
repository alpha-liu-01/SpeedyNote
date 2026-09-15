#!/usr/bin/env bash
# ============================================================================
# SpeedyNote HarmonyOS HAP Build Script
# ============================================================================
# Cross-compiles a Qt CMake project for HarmonyOS (arm64-v8a), packages it into
# a HAP, and optionally installs and launches it on a connected device.
#
# DevEco Studio is never invoked. It is only an IDE for ArkTS projects; the Qt
# flow generates its own DevEco project as a build artifact and drives packaging
# through the command line hvigor. DevEco's bundled JDK and emulator are used,
# but the application never opens.
#
# Pipeline:
#   qt-cmake            configure against the OHOS toolchain
#   cmake --build       produce lib<target>.so (Qt apps are shared modules here,
#                       not executables -- hence no CLI binary on this platform)
#   harmonydeployqt     materialise a DevEco project, stage Qt + 3rd-party libs
#   fix-icu-sonames.py  work around the ICU soname mismatch (see that script)
#   hvigorw assembleHap package the HAP
#   hdc install / aa start
#
# Prerequisites:
#   - HarmonyOS Command Line Tools at /opt/harmonyos/command-line-tools
#     This path is NOT configurable: Qt's prebuilt CMake config has it baked in
#     from Qt's own build. Override with OHOS_CLT only if you know your Qt was
#     configured elsewhere.
#   - Qt 6.12+ for HarmonyOS and the matching host Qt (see QT_VERSION below)
#   - ohos-additional-packages extracted to ~/.local/opt/ohos/additional-packages
#   - DevEco Studio installed (for its bundled JDK)
#
# Usage:
#   ./harmony/build-hap.sh                        # smoke test: Qt's widgets/gallery
#   ./harmony/build-hap.sh --run                  # ...and launch it on the device
#   ./harmony/build-hap.sh --source <dir> --run
#   ./harmony/build-hap.sh --clean --release --run
#
# Options:
#   --source <dir>  CMake project to build. Defaults to Qt's widgets/gallery
#                   example, which is the right subject for verifying the
#                   toolchain independently of SpeedyNote's own porting work.
#   --build-dir <d> Build directory (default: harmony/build-<name>)
#   --release       Build in Release mode (default: Debug)
#   --clean         Remove the build directory before configuring
#   --install       Install the HAP via hdc after packaging
#   --run           Install, then launch and tail the app's log
#   --no-package    Stop after cmake --build (skip deploy/package)
#   --verbose       Pass --verbose to harmonydeployqt
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

QT_VERSION="${QT_VERSION:-6.12.0}"
QT_ROOT="${QT_ROOT:-${HOME}/Qt/${QT_VERSION}}"
QT_HARMONY="${QT_ROOT}/harmonyos_arm64_v8a"

# The host Qt supplies harmonydeployqt, and its directory is named after the host
# platform. Probed rather than assumed so this runs on a Linux CI runner as well
# as on a developer's Mac; override QT_HOST to skip the guessing.
if [ -z "${QT_HOST:-}" ]; then
    for candidate in macos gcc_64 linux_gcc_64; do
        if [ -x "${QT_ROOT}/${candidate}/bin/harmonydeployqt" ]; then
            QT_HOST="${QT_ROOT}/${candidate}"
            break
        fi
    done
    QT_HOST="${QT_HOST:-${QT_ROOT}/macos}"
fi

OHOS_CLT="${OHOS_CLT:-/opt/harmonyos/command-line-tools}"
OHOS_SDK="${OHOS_CLT}/sdk/default/openharmony"
OHOS_ADDITIONAL_PACKAGES="${OHOS_ADDITIONAL_PACKAGES:-${HOME}/.local/opt/ohos/additional-packages}"
DEVECO_APP="${DEVECO_APP:-/Applications/DevEco-Studio.app}"

DEFAULT_SOURCE="${QT_ROOT%/*}/Examples/Qt-${QT_VERSION}/widgets/gallery"

# ---------- Argument parsing ----------
SOURCE_DIR="${DEFAULT_SOURCE}"
BUILD_DIR=""
BUILD_TYPE="Debug"
CLEAN=false
DO_INSTALL=false
DO_RUN=false
DO_PACKAGE=true
DEPLOY_VERBOSE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --source)     SOURCE_DIR="$2"; shift 2 ;;
        --build-dir)  BUILD_DIR="$2"; shift 2 ;;
        --release)    BUILD_TYPE="Release"; shift ;;
        --clean)      CLEAN=true; shift ;;
        --install)    DO_INSTALL=true; shift ;;
        --run)        DO_INSTALL=true; DO_RUN=true; shift ;;
        --no-package) DO_PACKAGE=false; shift ;;
        --verbose)    DEPLOY_VERBOSE="--verbose"; shift ;;
        -h|--help)
            sed -n '2,48p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

SOURCE_DIR="$(cd "${SOURCE_DIR}" && pwd)"
NAME="$(basename "${SOURCE_DIR}")"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build-${NAME}}"
# Absolute, because the deploy step below runs from inside the build directory
# and passes paths discovered here -- a relative --build-dir would resolve
# against the wrong directory there and harmondeployqt would report only
# "Failed to open input file".
mkdir -p "${BUILD_DIR}"
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"

# ---------- Preflight checks ----------
echo "=== SpeedyNote HarmonyOS Build (arm64-v8a, ${BUILD_TYPE}) ==="
echo ""

fail() { echo "ERROR: $*" >&2; exit 1; }

[ -x "${QT_HARMONY}/bin/qt-cmake" ] || fail "Qt for HarmonyOS not found at ${QT_HARMONY}.
Install the 'HarmonyOS' component under Qt ${QT_VERSION} via the Qt Maintenance Tool."

[ -x "${QT_HOST}/bin/harmonydeployqt" ] || fail "harmonydeployqt not found at ${QT_HOST}/bin.
It ships with the *host* Qt, so the matching Desktop component must also be installed."

[ -d "${OHOS_SDK}/native" ] || fail "HarmonyOS SDK not found at ${OHOS_SDK}.
Qt's prebuilt CMake config hardcodes this path; the command line tools must live at
${OHOS_CLT} exactly."

[ -d "${OHOS_ADDITIONAL_PACKAGES}/lib" ] || fail "ohos-additional-packages not found at ${OHOS_ADDITIONAL_PACKAGES}.
Download it from the Qt wiki and extract it there."

[ -f "${SOURCE_DIR}/CMakeLists.txt" ] || fail "no CMakeLists.txt in ${SOURCE_DIR}"

# ---------- Environment ----------
# ninja and cmake ship inside the SDK, node is bundled with the command line
# tools, and hvigor's PackageHap task shells out to a JAR. None of the three is
# exported by default, and each failure mode is obscure -- the Java one reports
# only "Unable to locate a Java Runtime".
# DevEco's bundled JBR is the convenient JDK on a developer machine; anywhere it
# is absent (CI, Linux) any JDK on PATH will do, so fall back to that rather than
# insisting on DevEco Studio.
if [ -z "${JAVA_HOME:-}" ]; then
    if [ -d "${DEVECO_APP}/Contents/jbr/Contents/Home" ]; then
        JAVA_HOME="${DEVECO_APP}/Contents/jbr/Contents/Home"
    elif command -v javac >/dev/null; then
        JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
    fi
fi
export JAVA_HOME
export PATH="${OHOS_SDK}/native/build-tools/cmake/bin:${OHOS_CLT}/tool/node/bin:${OHOS_CLT}/bin:${OHOS_SDK}/toolchains:${JAVA_HOME}/bin:${PATH}"

# Drives extra-libs-dirs in the deployment settings, which is what makes
# harmonydeployqt bundle fontconfig/freetype/ICU into the HAP. Passing only
# CMAKE_FIND_ROOT_PATH (as the Qt wiki suggests) satisfies the compiler but
# leaves the libraries out of the package: Qt's toolchain file seeds this from
# the paths its own CI used, then filters to ones that exist locally, so the
# vcpkg prefix silently drops out and nothing gets staged.
export QT_ADDITIONAL_PACKAGES_PREFIX_PATH="${OHOS_ADDITIONAL_PACKAGES}"

[ -d "${JAVA_HOME}" ] || fail "JDK not found at ${JAVA_HOME}. Set JAVA_HOME or install DevEco Studio."
command -v ninja >/dev/null || fail "ninja not found under ${OHOS_SDK}/native/build-tools/cmake/bin"
command -v hvigorw >/dev/null || fail "hvigorw not found under ${OHOS_CLT}/bin"

echo "Source:      ${SOURCE_DIR}"
echo "Build dir:   ${BUILD_DIR}"
echo "Qt:          ${QT_HARMONY}"
echo "OHOS SDK:    ${OHOS_SDK}"
echo "3rd-party:   ${OHOS_ADDITIONAL_PACKAGES}"
echo ""

# ---------- Configure and build ----------
if [ "${CLEAN}" = true ]; then
    echo "=== Cleaning ${BUILD_DIR} ==="
    rm -rf "${BUILD_DIR}"
fi

echo "=== Configuring ==="
"${QT_HARMONY}/bin/qt-cmake" \
    -S "${SOURCE_DIR}" \
    -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo ""
echo "=== Building ==="
cmake --build "${BUILD_DIR}" --parallel

if [ "${DO_PACKAGE}" = false ]; then
    echo ""
    echo "=== Done (--no-package) ==="
    ls -la "${BUILD_DIR}"/*.so 2>/dev/null || true
    exit 0
fi

# ---------- Deploy: generate the DevEco project and stage libraries ----------
SETTINGS="$(find "${BUILD_DIR}" -maxdepth 1 -name '*-harmony-deployment-settings.json' | head -1)"
[ -n "${SETTINGS}" ] || fail "no *-harmony-deployment-settings.json in ${BUILD_DIR}.
Does the project use qt_add_executable()?"

echo ""
echo "=== Staging DevEco project (harmonydeployqt) ==="
(cd "${BUILD_DIR}" && "${QT_HOST}/bin/harmonydeployqt" ${DEPLOY_VERBOSE} \
    --hvigor "$(command -v hvigorw)" \
    --input "${SETTINGS}")

HARMONY_PROJECT="$(find "${BUILD_DIR}" -maxdepth 1 -type d -name 'lib*-harmonyos' | head -1)"
[ -n "${HARMONY_PROJECT}" ] || fail "harmonydeployqt produced no lib*-harmonyos project in ${BUILD_DIR}"

# ---------- Work around the ICU soname mismatch ----------
echo ""
echo "=== Fixing ICU sonames ==="
"${SCRIPT_DIR}/fix-icu-sonames.py" "${HARMONY_PROJECT}/entry/libs/arm64-v8a"

# ---------- Request user-granted permissions from the generated ability ----------
echo ""
echo "=== Injecting permission request ==="
"${SCRIPT_DIR}/inject-permission-request.py" "${HARMONY_PROJECT}"

# ---------- Localized app labels ----------
echo ""
echo "=== Adding localized labels ==="
"${SCRIPT_DIR}/add-localized-labels.py" "${HARMONY_PROJECT}"

# ---------- Package ----------
# harmonydeployqt already ran assembleHap once, but that was before the two fixes
# above, so the HAP has to be rebuilt. hvigor is incremental, so this is cheap.
echo ""
echo "=== Packaging HAP ==="
# Redirect to a file rather than piping into grep: a pipe hands us grep's exit
# status, so an hvigor failure would slip through and the stale HAP left behind
# by harmonydeployqt's own assembleHap would be installed as if it were fresh --
# a patched .ets that fails to compile would then look like it had no effect.
HVIGOR_LOG="${BUILD_DIR}/hvigor-assembleHap.log"
if ! (cd "${HARMONY_PROJECT}" && hvigorw assembleHap --no-daemon) > "${HVIGOR_LOG}" 2>&1; then
    grep -E 'ERROR|Error Message' "${HVIGOR_LOG}" | head -20
    fail "hvigor failed to package the HAP. Full log: ${HVIGOR_LOG}"
fi
grep -E 'BUILD SUCCESSFUL|WARN: Will skip sign' "${HVIGOR_LOG}" || true

HAP="$(find "${HARMONY_PROJECT}/entry/build" -name '*.hap' | head -1)"
[ -n "${HAP}" ] || fail "no .hap produced under ${HARMONY_PROJECT}/entry/build"

BUNDLE="$(sed -n 's/.*"bundleName" *: *"\([^"]*\)".*/\1/p' "${HARMONY_PROJECT}/AppScope/app.json5" | head -1)"

echo ""
echo "=== Build Complete ==="
echo "HAP:          ${HAP}"
echo "Bundle name:  ${BUNDLE}"
echo "DevEco proj:  ${HARMONY_PROJECT}"
echo ""
echo "Note: the HAP is unsigned. R&D-mode emulators install it as-is; retail"
echo "hardware needs a Huawei-issued debug certificate bound to the device UDID."

# ---------- Install and run ----------
if [ "${DO_INSTALL}" = false ]; then
    exit 0
fi

command -v hdc >/dev/null || fail "hdc not found under ${OHOS_SDK}/toolchains"

if ! hdc list targets 2>/dev/null | grep -qv '^\[Empty\]$'; then
    fail "no device connected. Boot an emulator from DevEco's Device Manager, or
check HDC_SERVER_PORT if you have a second hdc server running."
fi

echo ""
echo "=== Installing ==="
hdc install -r "${HAP}"

if [ "${DO_RUN}" = false ]; then
    exit 0
fi

echo ""
echo "=== Launching ${BUNDLE} ==="
hdc shell hilog -r >/dev/null 2>&1 || true
hdc shell aa start -a QAbility -b "${BUNDLE}"
sleep 3

# hdc emits CRLF, so anchoring on end-of-line does not work here.
if hdc shell "ps -ef" 2>/dev/null | grep -q "[[:space:]]${BUNDLE}[[:space:]]*$"; then
    echo "Running."
else
    echo "WARNING: process is not alive. Newest faultlog (may predate this run):"
    hdc shell "ls -t /data/log/faultlog/faultlogger/ 2>/dev/null | head -1"
    echo "Read it with: hdc shell cat /data/log/faultlog/faultlogger/<name>"
fi

echo ""
echo "=== App log ==="
# The Qt template logs under the ohosQtTemplate tag; Qt's own qDebug output
# arrives on the same domain.
hdc shell hilog -x 2>/dev/null | grep -E 'ohosQtTemplate|ArkCompiler: (TypeError|Error)' | tail -20 || true
