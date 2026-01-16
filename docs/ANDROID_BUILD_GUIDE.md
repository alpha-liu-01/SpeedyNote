# SpeedyNote Android Build Guide

**Document Version:** 1.0  
**Date:** January 15, 2026  
**Status:** ✅ VERIFIED WORKING

---

## Overview

This guide provides step-by-step instructions for building SpeedyNote for Android using the Docker-based build system. The build produces an APK that can be installed on Android tablets and phones.

### Architecture

- **Target:** Android arm64-v8a (64-bit ARM)
- **PDF Backend:** MuPDF (cross-compiled)
- **UI Framework:** Qt 6.7.2 for Android
- **Minimum API:** 26 (Android 8.0)
- **Target API:** 34 (Android 14)

---

## Prerequisites

### Host System Requirements

- Linux host (Ubuntu 22.04+ recommended)
- Docker installed and running
- At least 20GB free disk space
- ADB (Android Debug Bridge) for device installation

### Android Device Requirements

- Android 8.0 (API 26) or higher
- arm64-v8a architecture (most modern devices)
- USB debugging enabled

---

## Quick Start

If you just want to build quickly and the Docker image already exists:

```bash
# Enter Docker container
./android/docker-shell.sh

# Inside container: build MuPDF (first time only)
./android/build-mupdf.sh

# Build SpeedyNote APK
./android/build-speedynote.sh

# Exit container and install on device
exit
adb install android/SpeedyNote.apk
```

---

## Detailed Build Instructions

### Phase 1: Build the Docker Image

The Docker image contains all build dependencies:
- Ubuntu 22.04 base
- Android SDK (API 34)
- Android NDK 26.1
- Qt 6.7.2 (Android arm64-v8a + Linux host tools)
- CMake, Ninja, OpenJDK 17

#### 1.1 Build the Docker image

```bash
cd /path/to/SpeedyNote
./android/docker-build.sh
```

This takes 10-20 minutes depending on your internet connection. The image is cached for subsequent builds.

#### 1.2 Verify the image

```bash
docker images | grep speedynote-android
```

You should see `speedynote-android-builder` in the list.

---

### Phase 2: Cross-compile MuPDF

MuPDF is the PDF rendering library used on Android (replacing Poppler from desktop).

#### 2.1 Enter the Docker container

```bash
./android/docker-shell.sh
```

This mounts the SpeedyNote source at `/workspace` inside the container.

#### 2.2 Build MuPDF

```bash
./android/build-mupdf.sh
```

This script:
1. Downloads MuPDF 1.24.10 source
2. Configures cross-compilation for Android arm64-v8a
3. Builds static libraries
4. Installs to `android/mupdf-build/`

Output files:
- `android/mupdf-build/lib/libmupdf.a`
- `android/mupdf-build/lib/libmupdf-third.a`
- `android/mupdf-build/include/mupdf/`

**Note:** This only needs to be done once. The built libraries persist on your host filesystem.

---

### Phase 3: Build SpeedyNote APK

#### 3.1 Run the build script

Still inside the Docker container:

```bash
./android/build-speedynote.sh
```

This script:
1. Configures CMake with Qt Android toolchain
2. Compiles all C++ source files
3. Links with MuPDF and Qt libraries
4. Runs `androiddeployqt` to create the APK
5. Signs the APK with a debug key

#### 3.2 Build output

The APK is created at:
- `android/build-app/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`
- Copied to: `android/SpeedyNote.apk`

#### 3.3 Exit the container

```bash
exit
```

---

### Phase 4: Install on Device

#### 4.1 Connect your Android device

1. Enable **Developer Options** on your device
2. Enable **USB Debugging**
3. Connect via USB cable
4. Accept the debugging prompt on the device

#### 4.2 Verify ADB connection

```bash
adb devices
```

You should see your device listed.

#### 4.3 Install the APK

```bash
adb install android/SpeedyNote.apk
```

For reinstalling after updates:
```bash
adb install -r android/SpeedyNote.apk
```

---

## Build Scripts Reference

### `android/docker-build.sh`

Builds the Docker image with all dependencies.

```bash
#!/bin/bash
docker build -t speedynote-android-builder android/
```

### `android/docker-shell.sh`

Enters an interactive shell in the Docker container.

```bash
#!/bin/bash
docker run -it --rm \
    -v "$(pwd):/workspace" \
    -w /workspace \
    speedynote-android-builder \
    /bin/bash
```

### `android/build-mupdf.sh`

Cross-compiles MuPDF for Android arm64-v8a.

Key environment variables:
- `ANDROID_NDK` - Path to NDK (default: `/opt/android-sdk/ndk/26.1.10909125`)
- `ANDROID_API` - Target API level (default: 26)

### `android/build-speedynote.sh`

Builds the full SpeedyNote APK.

Key environment variables:
- `QT_ANDROID` - Qt Android installation (default: `/opt/qt/6.7.2/android_arm64_v8a`)
- `QT_HOST` - Qt host tools (default: `/opt/qt/6.7.2/gcc_64`)
- `MUPDF_INCLUDE_DIR` - MuPDF headers
- `MUPDF_LIBRARIES` - MuPDF static libraries

---

## Directory Structure

```
SpeedyNote/
├── android/
│   ├── Dockerfile              # Docker image definition
│   ├── docker-build.sh         # Build Docker image
│   ├── docker-shell.sh         # Enter Docker container
│   ├── build-mupdf.sh          # Cross-compile MuPDF
│   ├── build-speedynote.sh     # Build full APK
│   ├── app-resources/          # Android app resources
│   │   ├── AndroidManifest.xml # App manifest
│   │   └── res/
│   │       └── xml/
│   │           └── file_paths.xml  # FileProvider paths
│   ├── mupdf-build/            # Built MuPDF (generated)
│   │   ├── include/
│   │   └── lib/
│   ├── build-app/              # CMake build directory (generated)
│   └── SpeedyNote.apk          # Final APK (generated)
├── source/
│   └── pdf/
│       ├── PdfProvider.h       # Abstract PDF interface
│       ├── PdfProviderFactory.cpp  # Platform selection
│       ├── MuPdfProvider.cpp   # Android: MuPDF backend
│       └── PopplerPdfProvider.cpp  # Desktop: Poppler backend
└── CMakeLists.txt              # Build configuration
```

---

## Platform-Specific Code

### PDF Provider Selection

The build system automatically selects the appropriate PDF backend:

```cmake
# In CMakeLists.txt
if(ANDROID)
    set(PDF_SOURCES
        source/pdf/PdfProviderFactory.cpp
        source/pdf/MuPdfProvider.cpp      # MuPDF for Android
    )
else()
    set(PDF_SOURCES
        source/pdf/PdfProviderFactory.cpp
        source/pdf/PopplerPdfProvider.cpp  # Poppler for desktop
    )
endif()
```

### Conditional Compilation

Several features are conditionally compiled for Android:

```cpp
// QSharedMemory not available on Android
#ifndef Q_OS_ANDROID
#include <QSharedMemory>
#endif

// Signal handlers only for desktop Linux
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
void setupLinuxSignalHandlers() { ... }
#endif

// Tests excluded from Android build
#ifndef Q_OS_ANDROID
#include "ui/ToolbarButtonTests.h"
#endif
```

---

## Troubleshooting

### Docker permission denied

**Error:** `permission denied while trying to connect to the Docker daemon socket`

**Fix:** Add your user to the docker group:
```bash
sudo usermod -aG docker $USER
newgrp docker
```

Or run Docker commands with `sudo`.

### QT_HOST_PATH not set

**Error:** `To use a cross-compiled Qt, please set the QT_HOST_PATH cache variable`

**Fix:** Ensure `QT_HOST` environment variable is correct:
```bash
export QT_HOST=/opt/qt/6.7.2/gcc_64
```

### APK installation fails with INSTALL_PARSE_FAILED_NO_CERTIFICATES

**Error:** APK is unsigned

**Fix:** The build script should sign automatically. If not, manually sign:
```bash
${QT_HOST}/bin/androiddeployqt \
    --input android-NoteApp-deployment-settings.json \
    --output android-build \
    --sign \
    --storepass android \
    --keypass android
```

### Resource not found: xml/file_paths

**Error:** `resource xml/file_paths not found`

**Fix:** Ensure `android/app-resources/res/xml/file_paths.xml` exists.

### Missing MuPDF libraries

**Error:** Linker can't find libmupdf.a

**Fix:** Run `./android/build-mupdf.sh` first.

### Poppler header not found (on Android build)

**Error:** `poppler/qt6/poppler-qt6.h not found`

**Fix:** Code still references Poppler. Check that:
1. `PdfProviderFactory.cpp` uses `#ifdef Q_OS_ANDROID` correctly
2. Source files include `PdfProvider.h`, not `PopplerPdfProvider.h`

---

## Clean Build

To perform a completely clean build:

```bash
# On host
rm -rf android/build-app
rm -rf android/mupdf-build

# Then rebuild
./android/docker-shell.sh
./android/build-mupdf.sh
./android/build-speedynote.sh
```

---

## Release Build

For production releases, you'll need to:

1. Create a release signing keystore
2. Update `build-speedynote.sh` with release signing credentials
3. Enable ProGuard/R8 for code shrinking
4. Add proper app icons in `android/app-resources/res/`

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-15 | Initial Android port with MuPDF backend |

---

## See Also

- [ANDROID_PORT_QA.md](ANDROID_PORT_QA.md) - Design decisions and Q&A
- [MuPDF Documentation](https://mupdf.com/docs/)
- [Qt for Android](https://doc.qt.io/qt-6/android.html)

