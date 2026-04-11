#!/bin/bash
# =============================================================================
# Fetch ML Kit Digital Ink Recognition frameworks via CocoaPods
# =============================================================================
# Downloads the GoogleMLKit/DigitalInkRecognition pod and all transitive
# dependencies, builds the source pods for device + simulator, then packages
# everything as .xcframework bundles in ios/mlkit-build/Frameworks/.
#
# CocoaPods distributes ML Kit as static .framework bundles (not xcframeworks)
# plus source-only dependency pods. This script:
#   1. Runs `pod install` to resolve and download everything
#   2. Builds source pods with xcodebuild for iphoneos + iphonesimulator
#   3. Converts vendored .framework fat binaries into .xcframework
#   4. Converts built .a static libs into .xcframework
#
# Prerequisites:
#   - CocoaPods (brew install cocoapods)
#   - Xcode command-line tools
#
# Usage (from the SpeedyNote project root):
#   ./ios/fetch-mlkit.sh
#
# Output:
#   ios/mlkit-build/Frameworks/*.xcframework
#
# To refresh: delete ios/mlkit-build/ and re-run.
# =============================================================================
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
MLKIT_VERSION="8.0.0"
OUTPUT_DIR="${SCRIPT_DIR}/mlkit-build/Frameworks"

# =============================================================================
# Check prerequisites
# =============================================================================
if ! command -v pod &> /dev/null; then
    echo -e "${RED}Error: CocoaPods not found.${NC}"
    echo "Install with: brew install cocoapods"
    exit 1
fi

if ! command -v xcodebuild &> /dev/null; then
    echo -e "${RED}Error: xcodebuild not found. Install Xcode command-line tools.${NC}"
    exit 1
fi

# =============================================================================
# Check if already fetched
# =============================================================================
if [ -d "${OUTPUT_DIR}" ] && [ "$(ls -A "${OUTPUT_DIR}"/*.xcframework 2>/dev/null)" ]; then
    echo -e "${YELLOW}ML Kit frameworks already present at ${OUTPUT_DIR}${NC}"
    echo "Delete ios/mlkit-build/ and re-run to refresh."
    exit 0
fi

echo -e "${CYAN}Fetching ML Kit Digital Ink Recognition ${MLKIT_VERSION}...${NC}"

# =============================================================================
# Create temp workspace with Podfile (no use_frameworks! → static libraries)
# =============================================================================
TEMP_DIR=$(mktemp -d)
trap "rm -rf '${TEMP_DIR}'" EXIT

cat > "${TEMP_DIR}/Podfile" <<PODFILE
platform :ios, '16.0'
target 'MlKitFetch' do
  pod 'GoogleMLKit/DigitalInkRecognition', '${MLKIT_VERSION}'
end
PODFILE

# Minimal Xcode project so CocoaPods is happy
mkdir -p "${TEMP_DIR}/MlKitFetch.xcodeproj"
cat > "${TEMP_DIR}/MlKitFetch.xcodeproj/project.pbxproj" <<'PBXPROJ'
// !$*UTF8*$!
{
    archiveVersion = 1;
    classes = {};
    objectVersion = 56;
    objects = {
        00000000000000000000001 = {
            isa = PBXProject;
            buildConfigurationList = 00000000000000000000004;
            compatibilityVersion = "Xcode 14.0";
            mainGroup = 00000000000000000000002;
            productRefGroup = 00000000000000000000003;
            projectDirPath = "";
            projectRoot = "";
            targets = (00000000000000000000005);
        };
        00000000000000000000002 = {
            isa = PBXGroup;
            children = ();
            sourceTree = "<group>";
        };
        00000000000000000000003 = {
            isa = PBXGroup;
            children = ();
            name = Products;
            sourceTree = "<group>";
        };
        00000000000000000000004 = {
            isa = XCConfigurationList;
            buildConfigurations = (00000000000000000000006);
            defaultConfigurationName = Release;
        };
        00000000000000000000005 = {
            isa = PBXNativeTarget;
            buildConfigurationList = 00000000000000000000007;
            buildPhases = ();
            name = MlKitFetch;
            productName = MlKitFetch;
            productType = "com.apple.product-type.application";
        };
        00000000000000000000006 = {
            isa = XCBuildConfiguration;
            buildSettings = {
                IPHONEOS_DEPLOYMENT_TARGET = 16.0;
                SDKROOT = iphoneos;
            };
            name = Release;
        };
        00000000000000000000007 = {
            isa = XCConfigurationList;
            buildConfigurations = (00000000000000000000008);
            defaultConfigurationName = Release;
        };
        00000000000000000000008 = {
            isa = XCBuildConfiguration;
            buildSettings = {
                PRODUCT_BUNDLE_IDENTIFIER = org.speedynote.mlkitfetch;
                PRODUCT_NAME = MlKitFetch;
                IPHONEOS_DEPLOYMENT_TARGET = 16.0;
            };
            name = Release;
        };
    };
    rootObject = 00000000000000000000001;
}
PBXPROJ

# =============================================================================
# Run CocoaPods
# =============================================================================
echo -e "${CYAN}Running pod install...${NC}"
cd "${TEMP_DIR}"
pod install --repo-update 2>&1 | tail -5

# =============================================================================
# Build source pods for device (arm64) and simulator (x86_64)
# =============================================================================
echo -e "${CYAN}Building source pods for iphoneos arm64...${NC}"
xcodebuild -project Pods/Pods.xcodeproj \
    -target "Pods-MlKitFetch" \
    -configuration Release \
    -sdk iphoneos \
    ARCHS="arm64" \
    ONLY_ACTIVE_ARCH=NO \
    BUILD_DIR="${TEMP_DIR}/xc-device" \
    -quiet 2>&1 | grep -E "error:|BUILD" || true

echo -e "${CYAN}Building source pods for iphonesimulator x86_64...${NC}"
xcodebuild -project Pods/Pods.xcodeproj \
    -target "Pods-MlKitFetch" \
    -configuration Release \
    -sdk iphonesimulator \
    ARCHS="x86_64" \
    ONLY_ACTIVE_ARCH=NO \
    BUILD_DIR="${TEMP_DIR}/xc-sim" \
    -quiet 2>&1 | grep -E "error:|BUILD" || true

DEVICE_DIR="${TEMP_DIR}/xc-device/Release-iphoneos"
SIM_DIR="${TEMP_DIR}/xc-sim/Release-iphonesimulator"

if [ ! -d "${DEVICE_DIR}" ] || [ ! -d "${SIM_DIR}" ]; then
    echo -e "${RED}Error: xcodebuild failed. Check Xcode installation.${NC}"
    exit 1
fi

# =============================================================================
# Create xcframeworks from vendored .framework bundles (fat → split → xcfw)
# =============================================================================
echo -e "${CYAN}Creating xcframeworks...${NC}"
mkdir -p "${OUTPUT_DIR}"
STAGE="${TEMP_DIR}/stage"
mkdir -p "${STAGE}/device" "${STAGE}/sim"
XCF_COUNT=0

for fw_path in $(find "${TEMP_DIR}/Pods" -path "*/Frameworks/*.framework" -type d); do
    fw_name=$(basename "$fw_path" .framework)
    binary="${fw_path}/${fw_name}"

    if [ ! -f "$binary" ]; then
        continue
    fi

    echo "  Packaging ${fw_name}.xcframework (vendored framework)"

    rm -rf "${STAGE}/device/${fw_name}.framework" "${STAGE}/sim/${fw_name}.framework"
    cp -R "$fw_path" "${STAGE}/device/${fw_name}.framework"
    cp -R "$fw_path" "${STAGE}/sim/${fw_name}.framework"

    lipo "$binary" -thin arm64 -output "${STAGE}/device/${fw_name}.framework/${fw_name}"
    lipo "$binary" -thin x86_64 -output "${STAGE}/sim/${fw_name}.framework/${fw_name}"

    rm -rf "${OUTPUT_DIR}/${fw_name}.xcframework"
    xcodebuild -create-xcframework \
        -framework "${STAGE}/device/${fw_name}.framework" \
        -framework "${STAGE}/sim/${fw_name}.framework" \
        -output "${OUTPUT_DIR}/${fw_name}.xcframework" \
        -quiet 2>/dev/null || \
    xcodebuild -create-xcframework \
        -framework "${STAGE}/device/${fw_name}.framework" \
        -framework "${STAGE}/sim/${fw_name}.framework" \
        -output "${OUTPUT_DIR}/${fw_name}.xcframework"

    XCF_COUNT=$((XCF_COUNT + 1))
done

# =============================================================================
# Create xcframeworks from built source pod static libraries
# =============================================================================
for device_lib in $(find "${DEVICE_DIR}" -name "lib*.a" -not -name "libPods-*" -type f); do
    lib_filename=$(basename "$device_lib")
    pod_name=$(basename "$(dirname "$device_lib")")
    sim_lib="${SIM_DIR}/${pod_name}/${lib_filename}"

    if [ ! -f "$sim_lib" ]; then
        echo -e "  ${YELLOW}Skipping ${lib_filename} (no simulator build)${NC}"
        continue
    fi

    xcf_name="${pod_name}.xcframework"
    echo "  Packaging ${xcf_name} (source pod)"

    rm -rf "${OUTPUT_DIR}/${xcf_name}"
    xcodebuild -create-xcframework \
        -library "$device_lib" \
        -library "$sim_lib" \
        -output "${OUTPUT_DIR}/${xcf_name}" \
        -quiet 2>/dev/null || \
    xcodebuild -create-xcframework \
        -library "$device_lib" \
        -library "$sim_lib" \
        -output "${OUTPUT_DIR}/${xcf_name}"

    XCF_COUNT=$((XCF_COUNT + 1))
done

# =============================================================================
# Extract resource bundles (needed at runtime by ML Kit)
# =============================================================================
RESOURCES_DIR="${SCRIPT_DIR}/mlkit-build/Resources"
mkdir -p "${RESOURCES_DIR}"
RES_COUNT=0
for res_bundle in $(find "${OUTPUT_DIR}" -name "*_resource.bundle" -type d | head -20); do
    res_name=$(basename "$res_bundle")
    if [ ! -d "${RESOURCES_DIR}/${res_name}" ]; then
        echo "  Extracting ${res_name}"
        cp -R "$res_bundle" "${RESOURCES_DIR}/${res_name}"
        RES_COUNT=$((RES_COUNT + 1))
    fi
done
if [ "$RES_COUNT" -gt 0 ]; then
    echo -e "${GREEN}Extracted ${RES_COUNT} resource bundle(s) to ${RESOURCES_DIR}${NC}"
fi

# =============================================================================
# Summary
# =============================================================================
echo ""
if [ "$XCF_COUNT" -gt 0 ]; then
    echo -e "${GREEN}Done! ${XCF_COUNT} xcframeworks created in ${OUTPUT_DIR}${NC}"
    ls -1 "${OUTPUT_DIR}"/*.xcframework 2>/dev/null | while read f; do echo "  $(basename "$f")"; done
    echo ""
    echo -e "${GREEN}You can now build SpeedyNote for iOS with ML Kit OCR support.${NC}"
else
    echo -e "${RED}Error: No xcframeworks were created. Check the output above for errors.${NC}"
    exit 1
fi
