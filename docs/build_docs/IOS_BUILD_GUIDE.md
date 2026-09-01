# SpeedyNote iPadOS Build Guide

**Document Version:** 1.1  
**Date:** April 2026  
**Status:** VERIFIED WORKING

---

## Overview

This guide provides step-by-step instructions for building SpeedyNote for iPadOS. The build system supports three targets: the iOS Simulator (for development), provisioned device builds (for testing on non-jailbroken iPads), and ad-hoc device builds (for distribution on jailbroken iPads via `.deb` or TrollStore via `.ipa`).

### Architecture

- **Target:** iPadOS arm64 (device), x86_64 (Intel Simulator)
- **PDF Backend:** MuPDF 1.24.10 (cross-compiled, statically linked)
- **OCR Backend:** Google ML Kit Digital Ink Recognition (statically linked)
- **UI Framework:** Qt 6.9.3 for iOS (statically linked)
- **Minimum Deployment:** iPadOS 16.0

All dependencies (Qt, MuPDF, ML Kit, Noto fonts) are statically linked into a single binary. There are no dynamic library dependencies beyond Apple system frameworks.

---

## Prerequisites

### Host System

- macOS 13+ (Ventura or later)
- Xcode 15+ with iOS Simulator runtime and command-line tools
- Homebrew (`brew`)
- ~15 GB free disk space (Qt, MuPDF source, build artifacts)

### Software Dependencies

| Dependency | Install | Used By |
|------------|---------|---------|
| Qt 6.9.3 for iOS | Qt Online Installer or `aqtinstall` | All builds |
| Xcode CLI tools | `xcode-select --install` | All builds |
| CocoaPods | `brew install cocoapods` | ML Kit OCR (fetching frameworks) |
| librsvg | `brew install librsvg` | Icon generation |
| ldid | `brew install ldid` | Ad-hoc device builds only |
| dpkg | `brew install dpkg` | `.deb` packaging only |

### Qt Installation

Qt 6.9.3 for iOS must be installed at `~/Qt/6.9.3/ios/`. The build scripts expect `qt-cmake` at `~/Qt/6.9.3/ios/bin/qt-cmake`.

Install via the Qt Online Installer (select the "iOS" component under Qt 6.9.3), or use `aqtinstall`:

```bash
pip install aqtinstall
aqt install-qt mac ios 6.9.3
```

---

## Quick Start

### Simulator (development)

```bash
# Cross-compile MuPDF for Simulator (first time only)
./ios/build-mupdf.sh --simulator

# Fetch ML Kit OCR frameworks (first time only, requires CocoaPods)
./ios/fetch-mlkit.sh

# Generate app icon (first time only)
./ios/generate-icons.sh

# Build and run in Simulator
./ios/build-sim.sh
./ios/run-sim.sh
```

### Device — provisioned (testing on a real iPad)

```bash
# Cross-compile MuPDF for device (first time only)
./ios/build-mupdf.sh

# Fetch ML Kit OCR frameworks (first time only, requires CocoaPods)
./ios/fetch-mlkit.sh

# Build, install, and run
./ios/build-device.sh YOUR_TEAM_ID
./ios/run-device.sh
```

### Device — ad-hoc (jailbreak / TrollStore distribution)

```bash
# Cross-compile MuPDF for device (first time only)
./ios/build-mupdf.sh

# Fetch ML Kit OCR frameworks (first time only, requires CocoaPods)
./ios/fetch-mlkit.sh

# Build Release, fake-sign with ldid
./ios/build-device.sh

# Package for distribution
./ios/build-deb.sh   # rootless .deb for Sileo
./ios/build-ipa.sh   # .ipa for TrollStore
```

---

## Detailed Build Instructions

### Phase 1: Generate App Icon

The icon is rendered from `resources/icons/mainicon.svg` into a 1024x1024 PNG for the Xcode Asset Catalog. Modern iOS auto-generates all other sizes.

```bash
./ios/generate-icons.sh
```

Requires `rsvg-convert` (`brew install librsvg`).

Output: `ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png`

This only needs to be done once (or when the icon SVG changes).

---

### Phase 2: Cross-compile MuPDF

MuPDF provides PDF rendering and export. It must be cross-compiled as a static library for the target platform.

The build script automatically downloads MuPDF 1.24.10 source, patches out the bundled HarfBuzz (to avoid symbol collisions with Qt's bundled copy), and compiles.

#### For Simulator

```bash
./ios/build-mupdf.sh --simulator
```

Output:
- `ios/mupdf-build-sim/lib/libmupdf.a`
- `ios/mupdf-build-sim/lib/libmupdf-third.a`
- `ios/mupdf-build-sim/include/mupdf/*.h`

#### For device

```bash
./ios/build-mupdf.sh
```

Output:
- `ios/mupdf-build/lib/libmupdf.a`
- `ios/mupdf-build/lib/libmupdf-third.a`
- `ios/mupdf-build/include/mupdf/*.h`

**Note:** The Simulator and device builds use different architectures (x86_64 vs arm64) and SDKs. If you need both, run the script twice with and without `--simulator`. The source tarball is shared and only downloaded once.

---

### Phase 2.5: Fetch ML Kit OCR Frameworks

Google ML Kit Digital Ink Recognition provides stroke-based handwriting recognition (OCR). The frameworks must be fetched via CocoaPods and converted into `.xcframework` bundles for static linking.

```bash
./ios/fetch-mlkit.sh
```

Requires `pod` (`brew install cocoapods`).

This script:
1. Creates a temporary CocoaPods project with `GoogleMLKit/DigitalInkRecognition` as a dependency
2. Runs `pod install` to download all ML Kit pods
3. Builds each pod's source into a static library using `xcodebuild`
4. Packages the resulting binaries (for both `iphoneos` and `iphonesimulator`) into `.xcframework` bundles using `xcodebuild -create-xcframework`
5. Copies ML Kit resource bundles (e.g. `DigitalInkRecognition_resource.bundle`)

Output:
- `ios/mlkit-build/xcframeworks/*.xcframework` — Static frameworks for all ML Kit components
- `ios/mlkit-build/resource_bundles/*.bundle` — ML Kit resource bundles

**Note:** This only needs to be done once (or when upgrading ML Kit). The `ios/mlkit-build/` directory is gitignored.

CMake automatically detects the xcframeworks at configure time and enables OCR support (`SPEEDYNOTE_ENABLE_MLKIT_OCR`). If the xcframeworks are not found, the build proceeds without OCR.

---

### Phase 3: Build SpeedyNote

#### 3A. Simulator Build

The Simulator build is the fastest way to iterate during development. No Apple ID or signing is required.

```bash
./ios/build-sim.sh
```

| Flag | Description |
|------|-------------|
| `--clean` | Remove build directory before configuring |
| `--rebuild` | Skip CMake configure step, just rebuild |
| `-h`, `--help` | Show usage information |

The build uses the Xcode generator (`-GXcode`) and targets `iphonesimulator` SDK. Output is a Debug `.app` bundle at `ios/build-sim/Debug-iphonesimulator/speedynote.app`.

#### 3B. Device Build — Provisioned

For testing on a non-jailbroken iPad you physically have access to. Requires a free or paid Apple Developer account.

```bash
./ios/build-device.sh YOUR_TEAM_ID
```

**Finding your Team ID:**  
Xcode → Settings → Accounts → select your Apple ID → the 10-character alphanumeric string is your Team ID.

| Flag | Description |
|------|-------------|
| `TEAM_ID` | Apple Development Team ID (first positional argument) |
| `--clean` | Remove build directory before configuring |
| `--rebuild` | Skip CMake configure step, just rebuild |
| `--release` | Build Release instead of Debug |
| `-h`, `--help` | Show usage information |

Without `--release`, provisioned builds are Debug by default (for Xcode debugging). Output: `ios/build-device/Debug-iphoneos/speedynote.app` (or `Release-iphoneos/` with `--release`).

**First-time device setup:**
1. Enable Developer Mode on the iPad: Settings → Privacy & Security → Developer Mode
2. Connect the iPad via USB-C and trust the computer
3. After first install, trust the developer certificate on the iPad: Settings → General → VPN & Device Management → tap your profile → Trust

#### 3C. Device Build — Ad-hoc

For distribution on jailbroken iPads or via TrollStore. No Apple ID needed.

```bash
./ios/build-device.sh
```

When no `TEAM_ID` is given, the script:
1. Builds in Release mode (always)
2. Passes `CODE_SIGN_IDENTITY=-` and `CODE_SIGNING_ALLOWED=NO` to Xcode
3. Strips debug symbols with `strip -x`
4. Fake-signs the binary with `ldid -S`
5. Verifies the signature with `ios/verify-signature.sh`

Requires `ldid` (`brew install ldid`).

> **Order matters.** `strip` rewrites the Mach-O, so stripping *after* signing leaves a
> CodeDirectory that hashes bytes which no longer exist. TrollStore re-signs `.ipa`
> payloads on install and so never notices, but a `.deb` install has nothing that fixes
> the signature up — AMFI validates it as-is and kills the app at launch. See
> [`.deb` installs crash immediately](#deb-installs-crash-immediately-but-the-ipa-works).

Output: `ios/build-device/Release-iphoneos/speedynote.app`

---

### Phase 4: Run

#### Simulator

```bash
./ios/run-sim.sh
```

| Flag | Description |
|------|-------------|
| `--list` | List available iPad Simulators |
| `DEVICE_NAME` | Specify a Simulator by name (e.g. `"iPad (A16)"`) |
| `-h`, `--help` | Show usage information |

If no device name is given, the first available iPad Simulator is auto-selected. The script boots the Simulator, opens Simulator.app, installs the `.app` bundle, and launches it.

**Note:** Apple Pencil input cannot be simulated. Use mouse/trackpad for basic UI navigation.

#### Device (provisioned)

```bash
./ios/run-device.sh
```

| Flag | Description |
|------|-------------|
| `--list` | List connected iOS devices |
| `--ipa` | Also create an `.ipa` package |
| `-h`, `--help` | Show usage information |

Alternatively, open the Xcode project and deploy with Cmd+R:

```bash
open ios/build-device/SpeedyNote.xcodeproj
```

---

### Phase 5: Package for Distribution

Packaging is only relevant for ad-hoc device builds (no `TEAM_ID`).

#### `.deb` — for jailbroken iPads (Sileo)

```bash
./ios/build-deb.sh
```

Requires `dpkg-deb` (`brew install dpkg`).

Creates a rootless `.deb` package that installs to `/var/jb/Applications/SpeedyNote.app/`. The package includes `postinst` and `postrm` scripts that run `uicache` to register/unregister the app icon.

Before packaging, the script verifies the ad-hoc signature and refuses to build a `.deb` that would install cleanly but crash on launch. After packaging it unpacks the finished `.deb` and re-checks the payload, so corruption is caught on the Mac rather than on the iPad.

Output: `ios/dist/SpeedyNote_<version>_iphoneos-arm64.deb`

**To install on a jailbroken iPad:**

```bash
scp ios/dist/SpeedyNote_1.2.5_iphoneos-arm64.deb mobile@<ipad-ip>:/tmp/
ssh mobile@<ipad-ip> 'sudo dpkg -i /tmp/SpeedyNote_1.2.5_iphoneos-arm64.deb'
```

**To test the full Sileo path without touching the production APT server:**

```bash
./ios/serve-repo.sh          # generates Packages/Release, serves ios/dist on :8080
```

Then add `http://<mac-ip>:8080/` as a source in Sileo, install, launch, and remove the source afterwards. This exercises the same download-index-install-uicache path as the real repo, so a package that works here will work there.

#### `.ipa` — for TrollStore

```bash
./ios/build-ipa.sh
```

Creates a standard `.ipa` (zipped `Payload/SpeedyNote.app/` structure).

Output: `ios/dist/SpeedyNote_<version>.ipa`

Transfer the `.ipa` to the iPad via AirDrop or USB, then open with TrollStore.

---

## Automated Builds (GitHub Actions)

[`.github/workflows/build-ios.yml`](../../.github/workflows/build-ios.yml) builds both packages on a
macOS runner and attaches them to the GitHub release.

**Triggers:** pushing a `v*` tag, or a manual run from the Actions tab (`workflow_dispatch`).
Artifacts are uploaded on every run; they are attached to a release only for tag pushes.

**Outputs**, named exactly as the local scripts produce them:

- `SpeedyNote_<version>.ipa`
- `SpeedyNote_<version>_iphoneos-arm64.deb`

**No secrets are required.** The ad-hoc path signs with ldid, so unlike the Android workflow
there is no keystore or Apple credential involved.

### How the runner reproduces a local setup

`ios/build-device.sh` refers to Qt through absolute paths (`${HOME}/Qt/6.9.3/ios/bin/qt-cmake`).
`install-qt-action` installs into `${dir}/Qt/<version>/<arch>`, so the workflow passes
`dir: /Users/runner` and the scripts run unmodified. `extra: '--autodesktop'` adds the parallel
host Qt required for cross-compilation.

A verification step then asserts that `qt-cmake` and `Qt6Svg` are actually present and reports how
many `qtbase_*.qm` catalogs were found, so a Qt packaging change fails immediately with a clear
message rather than surfacing as a confusing CMake error or a silently English-only UI.

Because `ios/Assets.xcassets/` is gitignored, the workflow runs `ios/generate-icons.sh` on every
run; without it there would be no app icon or `Assets.car`.

### Caching

| Cached | Size | Key includes |
|--------|------|--------------|
| `ios/mupdf-build` | ~51 MB | MuPDF version, `build-mupdf.sh` hash, Xcode version |
| `ios/mlkit-build` | ~93 MB | ML Kit version, `fetch-mlkit.sh` hash, Xcode version |

`ios/mupdf-src` (~419 MB) is deliberately **not** cached: `build-mupdf.sh` runs `make clean` on
every invocation, so only the installed output is worth keeping.

Including the Xcode version in both keys means a runner image update rebuilds the static libraries
instead of silently reusing ones built against a different SDK.

### Release verification

`ios/build-deb.sh` already refuses to package a binary that fails `ios/verify-signature.sh`, and
re-checks the unpacked payload afterwards. The workflow adds an audit step that unpacks the
finished `.deb` and prints `ldid -e` for the binary inside it, so every release carries a record of
the entitlements it shipped with — which matters because a `.deb` install has nothing that
re-signs the app.

---

## Build Scripts Reference

All scripts are in the `ios/` directory and should be run from the SpeedyNote project root.

| Script | Description |
|--------|-------------|
| `ios/generate-icons.sh` | Render SVG icon to 1024x1024 PNG for Asset Catalog |
| `ios/build-mupdf.sh [--simulator]` | Cross-compile MuPDF for device (default) or Simulator |
| `ios/fetch-mlkit.sh` | Fetch ML Kit OCR frameworks via CocoaPods and build xcframeworks |
| `ios/build-sim.sh [--clean] [--rebuild]` | Configure and build for iOS Simulator |
| `ios/build-device.sh [TEAM_ID] [--clean] [--rebuild] [--release]` | Configure and build for device |
| `ios/run-sim.sh [--list] [DEVICE_NAME]` | Install and launch in Simulator |
| `ios/run-device.sh [--list] [--ipa]` | Install and launch on connected device |
| `ios/build-deb.sh` | Package ad-hoc build as rootless `.deb` |
| `ios/build-ipa.sh` | Package ad-hoc build as `.ipa` |
| `ios/verify-signature.sh <binary>` | Check that an ldid ad-hoc signature still matches the file |
| `ios/serve-repo.sh [PORT]` | Serve `ios/dist` as a throwaway local APT repo for Sileo testing |
| `ios/debug-device-log.sh [--raw] [--crash]` | Stream a connected iPad's syslog, highlighting sandbox/signing/lifecycle lines |

---

## Directory Structure

```
SpeedyNote/
├── ios/
│   ├── Info.plist                  # iOS app metadata
│   ├── LaunchScreen.storyboard     # Launch screen
│   ├── Assets.xcassets/            # Asset Catalog
│   │   └── AppIcon.appiconset/
│   │       ├── Contents.json
│   │       └── icon-1024.png       # Generated by generate-icons.sh
│   │
│   ├── generate-icons.sh           # Icon generator
│   ├── build-mupdf.sh             # MuPDF cross-compiler
│   ├── fetch-mlkit.sh             # ML Kit framework fetcher (CocoaPods)
│   ├── build-sim.sh               # Simulator build
│   ├── build-device.sh            # Device build (provisioned or ad-hoc)
│   ├── run-sim.sh                 # Simulator runner
│   ├── run-device.sh              # Device installer/launcher
│   ├── build-deb.sh               # .deb packager
│   ├── build-ipa.sh               # .ipa packager
│   │
│   ├── mupdf-src/                 # MuPDF source (downloaded, gitignored)
│   ├── mupdf-build/               # MuPDF device libs (generated)
│   ├── mupdf-build-sim/           # MuPDF Simulator libs (generated)
│   ├── mlkit-build/               # ML Kit xcframeworks + resources (generated)
│   │   ├── xcframeworks/          # .xcframework bundles
│   │   └── resource_bundles/      # ML Kit resource bundles
│   ├── build-sim/                 # Simulator CMake/Xcode build (generated)
│   ├── build-device/              # Device CMake/Xcode build (generated)
│   └── dist/                      # Packaged .deb and .ipa (generated)
│
├── source/
│   ├── ios/
│   │   ├── IOSPlatformHelper.h/mm  # Dark mode, fonts, gesture fixes
│   │   ├── IOSShareHelper.h/mm     # Share sheet (export)
│   │   ├── PdfPickerIOS.h/mm       # Native PDF file picker
│   │   └── SnbxPickerIOS.h/mm      # Native SNBX file picker
│   └── ocr/engines/
│       ├── MlKitOcrEngine.h         # ML Kit OCR engine (shared header)
│       ├── MlKitOcrEngine.cpp       # ML Kit OCR engine (shared logic)
│       └── MlKitOcrEngine_ios.mm    # iOS Objective-C++ bridge
│
├── resources/icons/
│   └── mainicon.svg                # Source icon
│
└── CMakeLists.txt                  # Build configuration (iOS section)
```

---

## Platform-Specific Notes

### Static Linking

Qt for iOS is always statically linked (Apple policy for third-party frameworks). This means:
- The entire Qt runtime is compiled into the single `speedynote` binary
- No `.framework` or `.dylib` files need to be shipped in the `.app` bundle
- The binary links only against Apple system frameworks (UIKit, CoreGraphics, Metal, Network, etc.)
- MuPDF and its embedded Noto fonts are also statically linked
- ML Kit OCR frameworks are statically linked; resource bundles are copied into the `.app` bundle

### HarfBuzz Conflict

MuPDF bundles its own HarfBuzz with a custom allocator that requires an `fz_context*`. Qt also bundles HarfBuzz. When both are statically linked, the symbols collide and Qt ends up calling MuPDF's HarfBuzz without the required context, crashing in `fz_calloc_no_throw`.

The `build-mupdf.sh` script patches this by stripping MuPDF's bundled HarfBuzz source files from the build. SpeedyNote only uses MuPDF for PDF rendering/export and never invokes the HTML/EPUB layout engine (the only MuPDF consumer of HarfBuzz).

### Code Signing Modes

| Mode | Signing | Use Case |
|------|---------|----------|
| Simulator | None required | Development |
| Provisioned | Apple Development (automatic) | Testing on a physical iPad |
| Ad-hoc | `ldid -S` (fake-sign) | Jailbroken iPads, TrollStore |

### Conditional Compilation

iOS-specific code is guarded with `Q_OS_IOS`:

```cpp
#ifdef Q_OS_IOS
#include "ios/IOSPlatformHelper.h"
#include "ios/IOSShareHelper.h"
#endif
```

Features disabled on iOS:
- `QLocalServer` single-instance locking (not supported in iOS sandbox)
- `QProcess::startDetached` (not available on iOS)
- SpeedyNote CLI (iOS has no terminal)

### OCR (ML Kit Digital Ink Recognition)

When `ios/mlkit-build/xcframeworks/` contains the required `.xcframework` bundles, CMake sets `SPEEDYNOTE_ENABLE_MLKIT_OCR=ON` and compiles the Objective-C++ bridge (`MlKitOcrEngine_ios.mm`). The build automatically:
- Links all ML Kit xcframeworks and Apple's `Network.framework`
- Passes the `-ObjC` linker flag (required for Objective-C categories in static libraries)
- Copies ML Kit resource bundles into the `.app` bundle

At runtime, ML Kit downloads language models on-demand (requires network access on first use per language). The downloaded models are cached locally by ML Kit in the app's data directory.

---

## Troubleshooting

### Qt not found

**Error:** `Qt 6.9.3 for iOS not found at ~/Qt/6.9.3/ios/bin/qt-cmake`

**Fix:** Install Qt 6.9.3 with the iOS component via the Qt Online Installer or `aqtinstall`.

### MuPDF not found

**Error:** `MuPDF (device) not found at ios/mupdf-build/lib/libmupdf.a`

**Fix:** Run the MuPDF cross-compilation script:
```bash
./ios/build-mupdf.sh              # for device
./ios/build-mupdf.sh --simulator  # for Simulator
```

### Simulator: "No iPad Simulator found"

**Fix:** Install an iOS Simulator runtime in Xcode: Settings → Platforms → download an iOS runtime.

List available simulators:
```bash
./ios/run-sim.sh --list
```

### Device: "ldid not found"

**Fix:** Install ldid (only needed for ad-hoc builds):
```bash
brew install ldid
```

### `.deb` installs crash immediately (but the `.ipa` works)

**Symptom:** Sileo installs the package and the icon appears, but launching the app shows a
splash and returns to the home screen instantly. The same build installed as an `.ipa` via
TrollStore works perfectly.

**Cause:** an invalid ad-hoc code signature. TrollStore re-signs the main binary on install,
so it repairs whatever the build produced — that is why the `.ipa` path hides the problem.
A `.deb` install has no such step: `dpkg` just unpacks files, and AMFI validates the
embedded signature at `exec`. If the CDHash does not match the bytes on disk, the kernel
sends `SIGKILL` before any application code runs, so there is no Qt log and no useful
crash report beyond a `CODESIGNING` termination reason.

The usual trigger is modifying the binary after signing it — most often running `strip`
after `ldid`.

**Diagnose on the Mac:**

```bash
./ios/verify-signature.sh ios/build-device/Release-iphoneos/speedynote.app/speedynote
```

A stale signature reports a CodeDirectory page count that does not match the file's code
limit.

**Fix:** re-sign the (already stripped) binary, then repackage:

```bash
ldid -Sios/entitlements.plist ios/build-device/Release-iphoneos/speedynote.app/speedynote
./ios/build-deb.sh
```

`build-device.sh` now strips before signing and verifies the result, and `build-deb.sh`
refuses to package a binary that fails verification.

**Confirm on-device** (SIGKILL-at-exec leaves a distinctive trace):

```bash
ssh mobile@<ipad-ip>
ls -t /var/mobile/Library/Logs/CrashReporter | head
```

### `.deb` install shows a black screen (but the `.ipa` works)

**Symptom:** the app installs from Sileo, launches, and stays on a black screen. It does
*not* crash — the process stays alive and iOS never kills it.

**Cause:** the sandbox is denying the app access to the GPU. `ios/entitlements.plist` marks
the app `platform-application`, which is required for an app installed under
`/var/jb/Applications` but also means it is judged against the *system* sandbox policy
rather than the permissive profile ordinary third-party apps get. Under that policy, IOKit
user clients must be named explicitly. Without them, Metal device creation returns nil and
the app renders nothing while otherwise running normally.

TrollStore installs the `.ipa` into a normal app bundle location where the standard app
profile applies, so the GPU is allowed automatically — which is why the `.ipa` is unaffected.

**Confirm it:**

```bash
./ios/debug-device-log.sh
```

Launch the app and look for:

```
kernel(Sandbox) <Error>: System Policy: speedynote(NNN) deny(1) \
    iokit-open-user-client AGXDeviceUserClient
kernel(Sandbox) <Error>: System Policy: speedynote(NNN) deny(1) \
    iokit-open-user-client IOSurfaceRootUserClient
```

**Fix:** `ios/entitlements.plist` declares `com.apple.security.iokit-user-client-class`
listing the GPU, IOSurface, framebuffer and HID user clients. `ios/verify-signature.sh`
fails the build if `AGXDeviceUserClient` or `IOSurfaceRootUserClient` is absent.

Two things are easy to get wrong here:

- The key is `com.apple.security.iokit-user-client-class` — *not* under `com.apple.private`.
- iOS 15+ reads the **DER** entitlements blob (slot 7 of the signature superblob) and
  silently ignores an XML-only blob. ldid 2.1.5+ emits both; the verifier checks for the
  DER blob so an older ldid cannot produce a package whose entitlements do nothing.

Kodi/XBMC fixed the identical problem on jailbroken iPads in
[commit d5dc244](https://github.com/Memphiz/xbmc/commit/d5dc244c7e34dd89b9f79a228f68b31137feed7d).

### Device: provisioning failure

**Error:** Xcode signing or provisioning profile errors.

**Fix:**
1. Verify your Team ID: Xcode → Settings → Accounts → your Apple ID
2. Ensure Developer Mode is enabled on the iPad
3. Trust the developer certificate: Settings → General → VPN & Device Management

### Architecture mismatch

**Error:** Linker warnings about wrong architecture (e.g., linking x86_64 MuPDF into arm64 device build).

**Fix:** MuPDF must be compiled separately for Simulator and device:
- `ios/mupdf-build/` — device (arm64)
- `ios/mupdf-build-sim/` — Simulator (x86_64)

Use `--clean` to force a full reconfigure if you switched targets:
```bash
./ios/build-device.sh --clean
```

### ML Kit: CocoaPods not found

**Error:** `pod: command not found`

**Fix:** Install CocoaPods:
```bash
brew install cocoapods
```

### ML Kit: fetch-mlkit.sh copies 0 frameworks

**Error:** The script runs but produces no `.xcframework` output.

**Fix:** Ensure CocoaPods can download dependencies (requires internet access). Delete the temporary work directory and retry:
```bash
rm -rf ios/mlkit-build
./ios/fetch-mlkit.sh
```

### ML Kit: OCR not enabled in build

**Symptom:** SpeedyNote builds but the OCR/scan button is not available.

**Fix:** CMake could not find ML Kit xcframeworks. Run `ios/fetch-mlkit.sh` and then rebuild with `--clean`:
```bash
./ios/fetch-mlkit.sh
./ios/build-device.sh --clean   # or ./ios/build-sim.sh --clean
```

### ML Kit: `doesNotRecognizeSelector` crash

**Error:** `___forwarding___` crash in ML Kit code at runtime.

**Fix:** This is caused by Objective-C categories not being loaded from static libraries. Ensure the `-ObjC` linker flag is set in `CMakeLists.txt` (it should be set automatically when ML Kit is enabled). Rebuild with `--clean`.

---

## Clean Build

To perform a completely clean build:

```bash
# Remove all generated build artifacts
rm -rf ios/build-sim ios/build-device ios/dist

# Optionally remove MuPDF build (requires recompilation)
rm -rf ios/mupdf-build ios/mupdf-build-sim

# Optionally remove ML Kit build (requires re-fetching)
rm -rf ios/mlkit-build

# Rebuild from scratch
./ios/build-mupdf.sh --simulator   # if targeting Simulator
./ios/build-mupdf.sh               # if targeting device
./ios/fetch-mlkit.sh               # fetch ML Kit frameworks
./ios/build-sim.sh --clean          # or ./ios/build-device.sh --clean
```

---

## See Also

- [IOS_BUG_TRACKING.md](../IOS_BUG_TRACKING.md) — Known issues and fixes
- [ANDROID_BUILD_GUIDE.md](ANDROID_BUILD_GUIDE.md) — Android build guide (similar architecture)
- [MuPDF Documentation](https://mupdf.com/docs/)
- [Qt for iOS](https://doc.qt.io/qt-6/ios.html)
