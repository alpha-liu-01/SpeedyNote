# SpeedyNote iPadOS Build Guide

**Document Version:** 1.0  
**Date:** February 2026  
**Status:** ✅ VERIFIED WORKING

---

## Overview

This guide provides step-by-step instructions for building SpeedyNote for iPadOS. The build system supports three targets: the iOS Simulator (for development), provisioned device builds (for testing on non-jailbroken iPads), and ad-hoc device builds (for distribution on jailbroken iPads via `.deb` or TrollStore via `.ipa`).

### Architecture

- **Target:** iPadOS arm64 (device), x86_64 (Intel Simulator)
- **PDF Backend:** MuPDF 1.24.10 (cross-compiled, statically linked)
- **UI Framework:** Qt 6.9.3 for iOS (statically linked)
- **Minimum Deployment:** iPadOS 16.0

All dependencies (Qt, MuPDF, Noto fonts) are statically linked into a single binary. There are no dynamic library dependencies beyond Apple system frameworks.

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

# Build, install, and run
./ios/build-device.sh YOUR_TEAM_ID
./ios/run-device.sh
```

### Device — ad-hoc (jailbreak / TrollStore distribution)

```bash
# Cross-compile MuPDF for device (first time only)
./ios/build-mupdf.sh

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
3. Fake-signs the binary with `ldid -S`
4. Strips debug symbols with `strip -x`

Requires `ldid` (`brew install ldid`).

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

Output: `ios/dist/SpeedyNote_<version>_iphoneos-arm64.deb`

**To install on a jailbroken iPad:**

```bash
scp ios/dist/SpeedyNote_1.2.4_iphoneos-arm64.deb root@<ipad-ip>:/tmp/
ssh root@<ipad-ip> 'dpkg -i /tmp/SpeedyNote_1.2.4_iphoneos-arm64.deb'
```

Or host the `.deb` on an APT repository for installation via Sileo.

#### `.ipa` — for TrollStore

```bash
./ios/build-ipa.sh
```

Creates a standard `.ipa` (zipped `Payload/SpeedyNote.app/` structure).

Output: `ios/dist/SpeedyNote_<version>.ipa`

Transfer the `.ipa` to the iPad via AirDrop or USB, then open with TrollStore.

---

## Build Scripts Reference

All scripts are in the `ios/` directory and should be run from the SpeedyNote project root.

| Script | Description |
|--------|-------------|
| `ios/generate-icons.sh` | Render SVG icon to 1024x1024 PNG for Asset Catalog |
| `ios/build-mupdf.sh [--simulator]` | Cross-compile MuPDF for device (default) or Simulator |
| `ios/build-sim.sh [--clean] [--rebuild]` | Configure and build for iOS Simulator |
| `ios/build-device.sh [TEAM_ID] [--clean] [--rebuild] [--release]` | Configure and build for device |
| `ios/run-sim.sh [--list] [DEVICE_NAME]` | Install and launch in Simulator |
| `ios/run-device.sh [--list] [--ipa]` | Install and launch on connected device |
| `ios/build-deb.sh` | Package ad-hoc build as rootless `.deb` |
| `ios/build-ipa.sh` | Package ad-hoc build as `.ipa` |

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
│   ├── build-sim/                 # Simulator CMake/Xcode build (generated)
│   ├── build-device/              # Device CMake/Xcode build (generated)
│   └── dist/                      # Packaged .deb and .ipa (generated)
│
├── source/
│   └── ios/
│       ├── IOSPlatformHelper.h/mm  # Dark mode, fonts, gesture fixes
│       ├── IOSShareHelper.h/mm     # Share sheet (export)
│       ├── PdfPickerIOS.h/mm       # Native PDF file picker
│       └── SnbxPickerIOS.h/mm      # Native SNBX file picker
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
- The binary links only against Apple system frameworks (UIKit, CoreGraphics, Metal, etc.)
- MuPDF and its embedded Noto fonts are also statically linked

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
- SDL2 game controller support
- SpeedyNote CLI (iOS has no terminal)

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

---

## Clean Build

To perform a completely clean build:

```bash
# Remove all generated build artifacts
rm -rf ios/build-sim ios/build-device ios/dist

# Optionally remove MuPDF build (requires recompilation)
rm -rf ios/mupdf-build ios/mupdf-build-sim

# Rebuild from scratch
./ios/build-mupdf.sh --simulator   # if targeting Simulator
./ios/build-mupdf.sh               # if targeting device
./ios/build-sim.sh --clean          # or ./ios/build-device.sh --clean
```

---

## See Also

- [IOS_BUG_TRACKING.md](../IOS_BUG_TRACKING.md) — Known issues and fixes
- [ANDROID_BUILD_GUIDE.md](ANDROID_BUILD_GUIDE.md) — Android build guide (similar architecture)
- [MuPDF Documentation](https://mupdf.com/docs/)
- [Qt for iOS](https://doc.qt.io/qt-6/ios.html)
