#!/bin/bash
# =============================================================================
# Fetch ML Kit Digital Ink Recognition frameworks via CocoaPods
# =============================================================================
# Downloads the GoogleMLKit/DigitalInkRecognition pod and all transitive
# dependencies, then copies the resulting .xcframework bundles into
# ios/mlkit-build/Frameworks/ for CMake to link.
#
# Prerequisites:
#   - CocoaPods (gem install cocoapods)
#   - Xcode command-line tools
#
# Usage (from the SpeedyNote project root):
#   ./ios/fetch-mlkit.sh
#
# Output:
#   ios/mlkit-build/Frameworks/*.xcframework
#
# =============================================================================
set -e

# Colors for output
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
    echo "Install with: sudo gem install cocoapods"
    echo "Or with Homebrew: brew install cocoapods"
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
# Create temp workspace with Podfile
# =============================================================================
TEMP_DIR=$(mktemp -d)
trap "rm -rf '${TEMP_DIR}'" EXIT

cat > "${TEMP_DIR}/Podfile" <<'PODFILE'
platform :ios, '16.0'
target 'MlKitFetch' do
  use_frameworks!
  pod 'GoogleMLKit/DigitalInkRecognition', '8.0.0'
end
PODFILE

# Create a minimal Xcode project so CocoaPods is happy
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
# Copy xcframeworks to output
# =============================================================================
echo -e "${CYAN}Copying frameworks to ${OUTPUT_DIR}...${NC}"
mkdir -p "${OUTPUT_DIR}"

# CocoaPods downloads xcframeworks into Pods/ subdirectories
find "${TEMP_DIR}/Pods" -name "*.xcframework" -type d | while read fw; do
    fw_name=$(basename "$fw")
    if [ ! -d "${OUTPUT_DIR}/${fw_name}" ]; then
        cp -R "$fw" "${OUTPUT_DIR}/"
        echo "  Copied ${fw_name}"
    fi
done

# Count frameworks
FW_COUNT=$(ls -d "${OUTPUT_DIR}"/*.xcframework 2>/dev/null | wc -l | tr -d ' ')
echo ""
echo -e "${GREEN}Done! ${FW_COUNT} frameworks copied to ${OUTPUT_DIR}${NC}"
echo -e "${GREEN}You can now build SpeedyNote for iOS with ML Kit OCR support.${NC}"
