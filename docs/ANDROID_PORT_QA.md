# SpeedyNote Android Port - Q&A Document

**Document Version:** 1.0  
**Date:** January 15, 2026  
**Status:** 🔵 PLANNING PHASE  
**Prerequisites:** Migration complete, new architecture (DocumentViewport) only

---

## Overview

This document tracks design decisions and questions for porting SpeedyNote to Android using the new DocumentViewport-based architecture.

---

## Q1: Which PDF backend should we use?

**Options:**

| Option | Description |
|--------|-------------|
| **A) Poppler (cross-compiled)** | Same as desktop, full feature parity |
| **B) Android PdfRenderer** | Native Android API, no external dependencies |
| **C) MuPDF** | Lightweight PDF library, good feature set |

**Decision:** Option B (Android PdfRenderer) if it covers current features, otherwise Option C (MuPDF).

### Q1.1: What features does Android PdfRenderer provide?

**Android PdfRenderer capabilities (API 21+):**

| Feature | PdfRenderer Support | SpeedyNote Needs |
|---------|---------------------|------------------|
| Render page to bitmap | ✅ Yes | ✅ Required |
| Page count | ✅ Yes | ✅ Required |
| Page dimensions | ✅ Yes | ✅ Required |
| Text extraction | ❌ No | 🟡 Used by Highlighter |
| Text box positions | ❌ No | 🟡 Used by Highlighter |
| Internal links (TOC) | ❌ No | 🟡 Used by OutlinePanel |
| Clickable links | ❌ No | 🟡 Used by Highlighter tool |
| Outline/Bookmarks | ❌ No | 🟡 Used by OutlinePanel |

**Conclusion:** PdfRenderer is **insufficient** for SpeedyNote's current feature set. The Highlighter tool relies on text extraction, and the OutlinePanel needs TOC data.

### Q1.2: What features does MuPDF provide?

**MuPDF capabilities:**

| Feature | MuPDF Support | Notes |
|---------|---------------|-------|
| Render page to bitmap | ✅ Yes | High quality |
| Page count/dimensions | ✅ Yes | |
| Text extraction | ✅ Yes | Via `fz_stext_page` |
| Text box positions | ✅ Yes | Character-level boxes |
| Internal links | ✅ Yes | `fz_load_links()` |
| Clickable links | ✅ Yes | Goto + URI links |
| Outline/Bookmarks | ✅ Yes | `fz_load_outline()` |
| Annotations | ✅ Yes | Full annotation support |

**MuPDF advantages over Poppler for Android:**
- Smaller binary (~5MB vs ~15MB for Poppler)
- No complex dependency chain (Poppler needs fontconfig, cairo, etc.)
- Designed for embedded/mobile use
- AGPL license (same as Poppler)

**Conclusion:** **MuPDF is the recommended choice** for Android.

### Q1.3: What's the implementation plan for MuPDF?

1. Create `MuPdfProvider` class implementing `PdfProvider` interface
2. Wrap MuPDF's C API with Qt types
3. Cross-compile MuPDF for Android (simpler than Poppler - fewer deps)
4. Conditional compilation: Use Poppler on desktop, MuPDF on Android

```cpp
// In PdfProvider::create()
#ifdef Q_OS_ANDROID
    return std::make_unique<MuPdfProvider>(filePath);
#else
    return std::make_unique<PopplerPdfProvider>(filePath);
#endif
```

---

## Q2: Build system - Qt Creator Android kit vs Docker?

**Options:**

| Option | Description |
|--------|-------------|
| **A) Qt Creator Android Kit** | Official Qt tooling, GUI-based |
| **B) Docker (community approach)** | Reproducible, CI-friendly |
| **C) Command-line with distro packages** | No Qt account, uses system packages |

**Decision:** Use package manager approach if possible, otherwise Docker.

### Q2.1: Does Qt Creator's Android kit require a Qt account?

**Yes, with caveats:**

| Qt Source | Account Required? | Notes |
|-----------|-------------------|-------|
| Qt Online Installer | ✅ Yes | Required for downloading Android kits |
| Distro packages (apt/pacman) | ❌ No | But usually lacks Android cross-compile support |
| Build from source | ❌ No | Complex but no account needed |
| Docker (community) | ❌ No | Pre-built Qt for Android in image |

**The problem:** Linux distro packages (e.g., `qt6-base-dev`) only provide host builds, not Android cross-compilation toolchains. The Android kits must come from:
1. Qt Online Installer (requires account)
2. Building Qt from source for Android (complex)
3. Pre-built Docker images (what the community did)

### Q2.2: Recommendation

**Use the Docker-based approach** because:
1. No Qt account required
2. Reproducible builds (same environment every time)
3. CI/CD friendly
4. Community already proved it works
5. Isolates Android toolchain complexity

We can simplify the community's setup since we:
- Don't need Poppler (using MuPDF instead)
- Don't need SDL2
- Have cleaner code (no `#ifdef` spaghetti)

I agree with your recommendation. 
---

## Q3: Android API level considerations

**Decision:** Need to evaluate API level vs stylus performance trade-offs.

### Q3.1: Does higher API level improve stylus poll rate?

**Short answer:** Not directly, but indirectly yes.

**The 60Hz stylus issue is likely NOT an API level problem.** Here's what affects stylus poll rate:

| Factor | Impact | Notes |
|--------|--------|-------|
| **Hardware capability** | Primary | Device must support high-rate digitizer |
| **Qt's input handling** | Secondary | Qt batches touch events for efficiency |
| **`requestUnbufferedDispatch()`** | Critical | Disables Android's touch event batching |
| **Choreographer frame rate** | Matters | App must request high refresh rate |
| **Android API level** | Minor | Higher APIs have better stylus APIs |

### Q3.2: Key Android APIs for stylus performance

| API Level | Feature | Relevance |
|-----------|---------|-----------|
| 21 (5.0) | Basic stylus support | Minimum viable |
| 23 (6.0) | `MotionEvent.TOOL_TYPE_STYLUS` improvements | Better detection |
| 26 (8.0) | Low-latency graphics hint | Reduces drawing latency |
| 31 (12) | `requestUnbufferedDispatch()` for windows | **Critical for high poll rate** |
| 33 (13) | Stylus handwriting APIs | Not needed for SpeedyNote |
| 34 (14) | Improved stylus prediction | Nice to have |

### Q3.3: Why was the community port limited to 60Hz?

Likely causes (in order of probability):

1. **Qt's default touch event batching:** Qt coalesces touch events to match frame rate by default. Need to explicitly request unbuffered events.

2. **Missing `requestUnbufferedDispatch()`:** This Android API (API 31+) tells the system not to batch stylus events. Without it, you get one event per frame (~60Hz on 60Hz displays).

3. **VSync-limited rendering:** If the app only repaints on VSync, you only "see" 60 updates/second even if more events arrive.

4. **Old Qt version:** Older Qt versions had worse Android stylus support.

### Q3.4: Recommended API level

**Minimum: API 26 (Android 8.0)**
- Covers 95%+ of active devices
- Low-latency graphics hint available
- Better stylus support than API 23

**Target: API 31 (Android 12)**
- `requestUnbufferedDispatch()` for high-rate stylus input
- Modern permission model
- 75%+ of active devices

### Q3.5: How to achieve high stylus poll rate on Android?

1. **Request unbuffered input** (API 31+):
```java
// In Android Activity (via JNI or Qt Android Extras)
getWindow().getDecorView().requestUnbufferedDispatch(motionEvent);
```

2. **Disable Qt's touch event batching:**
```cpp
// In main() before QApplication
qputenv("QT_ANDROID_DISABLE_ACCESSIBILITY", "1");
// May need custom Qt build with modified QAndroidPlatformInputContext
```

3. **Use `QEvent::TouchUpdate` directly** instead of waiting for paint events

4. **Consider Qt 6.7+** which has improved Android stylus support

---

## Q4: Codebase scope

**Decision:** ✅ New architecture only (DocumentViewport). Old InkCanvas code will NOT be ported.

### Q4.1: What code is included in Android port?

**Included (new architecture):**
- `source/core/` - Document, Page, DocumentViewport
- `source/layers/` - VectorLayer
- `source/strokes/` - StrokePoint, VectorStroke  
- `source/objects/` - InsertedObject, ImageObject, LinkObject
- `source/pdf/` - PdfProvider interface (+ new MuPdfProvider)
- `source/ui/` - TabManager, LayerPanel, sidebars, toolbars, etc.

**Excluded (old/platform-specific):**
- `source/InkCanvas.*` - Legacy, deleted in Phase 5
- `source/VectorCanvas.*` - Test implementation, deleted in Phase 5
- `source/SDLControllerManager.*` - SDL2 controller (not on Android)
- `source/SimpleAudio.*` - ALSA/DirectSound audio

### Q4.2: Platform-specific code strategy

```cpp
// PdfProvider selection
#ifdef Q_OS_ANDROID
    // MuPDF implementation
#else
    // Poppler implementation
#endif

// Controller support (already exists in CMakeLists.txt)
option(ENABLE_CONTROLLER_SUPPORT "Enable SDL2 controllers" OFF)
// OFF for Android, optional for desktop
```

---

## Q5: Touch input on Android

### Q5.1: How does Qt handle Android touch/stylus events?

Qt translates Android `MotionEvent` to `QTouchEvent` or `QTabletEvent`:

| Android Event | Qt Event | SpeedyNote Handling |
|---------------|----------|---------------------|
| `ACTION_DOWN` (finger) | `QTouchEvent::TouchBegin` | TouchGestureHandler |
| `ACTION_DOWN` (stylus) | `QTabletEvent::TabletPress` | DocumentViewport |
| `ACTION_MOVE` | `TouchUpdate` / `TabletMove` | Stroke points added |
| `ACTION_UP` | `TouchEnd` / `TabletRelease` | Stroke finalized |

### Q5.2: Does `DocumentViewport` already handle tablet events?

**Yes!** The current implementation has:
- `tabletEvent(QTabletEvent*)` - handles stylus input
- `mousePressEvent/Move/Release` - handles mouse (and touch fallback)
- `handlePointerPress/Move/Release` - unified input handling
- Pressure sensitivity via `event->pressure()`
- Eraser detection via `QPointingDevice::PointerType::Eraser`

### Q5.3: What about palm rejection?

Android handles palm rejection at the OS level when a stylus is detected. Qt passes through the already-filtered events. No SpeedyNote changes needed.

---

## Q6: File system access on Android

### Q6.1: Where can SpeedyNote store documents on Android?

| Location | Access | Persistence | Notes |
|----------|--------|-------------|-------|
| App internal storage | Always | Until uninstall | Best for app data |
| App-specific external | Runtime permission (API<29) | Until uninstall | Good for user docs |
| Scoped storage (API 29+) | SAF (document picker) | Permanent | Required for shared files |

### Q6.2: What changes are needed for Android storage?

1. **Use Qt's standard paths:**
```cpp
// Works on Android
QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
```

2. **For opening PDFs:** Use Android's document picker via Qt:
```cpp
// Opens system file picker
QFileDialog::getOpenFileName(this, "Open PDF", "", "PDF Files (*.pdf)");
```

3. **For saving .snb bundles:** Save to app-specific storage, optionally export via SAF.

---

## Q7: APK packaging

### Q7.1: What goes into the APK?

| Component | Size (approx) | Notes |
|-----------|---------------|-------|
| SpeedyNote native lib | ~3-5 MB | Our C++ code |
| Qt6 libs | ~15-20 MB | Core, Gui, Widgets, etc. |
| MuPDF lib | ~5 MB | PDF rendering |
| Resources (icons, QSS) | ~2 MB | Assets |
| **Total** | **~25-32 MB** | Reasonable for a note app |

Compare to Poppler approach: Would add ~15MB more for Poppler + dependencies.

### Q7.2: How is the APK built?

Using `androiddeployqt` (Qt's Android packaging tool):
1. CMake builds the native library
2. `androiddeployqt` scans for Qt dependencies
3. Generates Gradle project with AndroidManifest.xml
4. Gradle builds the final APK

---

## Q8: Testing strategy

### Q8.1: How to test on Android?

| Method | Speed | Fidelity | Notes |
|--------|-------|----------|-------|
| Android Emulator | Slow | Low | No real stylus, poor touch |
| Physical device (USB) | Fast | High | Best for stylus testing |
| Physical device (WiFi ADB) | Fast | High | Wireless debugging |

### Q8.2: Recommended test devices

| Device Type | Why |
|-------------|-----|
| Samsung Galaxy Tab S series | Excellent S-Pen support, common |
| Google Pixel Tablet | Reference Android, good stylus |
| Xiaomi Pad series | Popular, affordable, good stylus |

I already have a Samsung Galaxy Tab S6 Lite 2024 on Lineage OS 23.1, Android 16 (which has poor performance but a suprisingly good stylus, and the same stylus even works on my wacom one tablet, so we can directly compare the results)
---

## Resolved Questions

### Q9: Qt version for Android?

What Qt version should we target?
- Qt 6.5 LTS (stable, widely supported)
- Qt 6.6/6.7 (better stylus support, newer)

**User:** If we can get a docker for a higher qt version (like 6.10 or something), it would be the best. I already use 6.10 on the Windows build. But since I'm using kubuntu 24.04.3 LTS right now, and most Linux users don't have Qt 6.10 available through the package manager, the Linux build is stuck at 6.4.

**Advice:** 

| Qt Version | Stylus Support | Android Support | Recommendation |
|------------|---------------|-----------------|----------------|
| Qt 6.4 | Basic | Good | ❌ Too old |
| Qt 6.5 LTS | Improved | Good | 🟡 Safe fallback |
| Qt 6.7 | Much better | Good | ✅ Sweet spot |
| Qt 6.8+ | Best | Good | ✅ If available |

**Key improvements in Qt 6.7+ for Android stylus:**
- Better `QTabletEvent` handling on Android
- Improved pressure curve accuracy
- Fixed stylus hover events
- Better palm rejection passthrough

**Recommendation:** Build the Docker image with **Qt 6.7 or 6.8**. The community's Docker used whatever version they had, but since we're building fresh, we can choose. Qt 6.7 is widely available and has the stylus improvements we need.

**Note:** Linux desktop can stay at 6.4 (package manager version) while Android uses 6.7+ in Docker. They're independent builds.

**Decision:** ✅ **Qt 6.7** for Android Docker image (or 6.8 if easily available)

---

### Q10: Multi-ABI support?

Should we build for multiple ABIs?
- `arm64-v8a` only (modern devices, simpler)
- `arm64-v8a` + `armeabi-v7a` (broader compatibility, larger APK)

**User:** I don't even think Qt6 can support 32-bit. From the Qt documents, it only supports 64-bit. If we can actually get a 32-bit version working, it would definitely be nice to have.

**Advice:** You're correct! **Qt 6 dropped 32-bit Android support.**

From Qt documentation:
> "Qt for Android supports the following ABIs: arm64-v8a, x86_64. 32-bit ABIs (armeabi-v7a, x86) are no longer supported in Qt 6."

Also, Google Play requires 64-bit since August 2019, and most devices from 2015+ are 64-bit.

**Decision:** ✅ **arm64-v8a only** (Qt6 doesn't support 32-bit anyway, and 99%+ of target devices are 64-bit)

---

### Q11: MuPDF licensing?

MuPDF is AGPL. SpeedyNote is also AGPL. Compatibility confirmed?

**User:** Yes.

**Decision:** ✅ **Confirmed compatible** (both AGPL-3.0)

---

### Q12: Markdown editor on Android?

Does QMarkdownTextEdit work well with Android's soft keyboard?

**User:** I don't know yet (because when the community member made the port, the markdown note sidebar wasn't implemented yet), but the markdown notes isn't really a key feature, so we can ignore that for now.

**Decision:** ✅ **Deferred** - Not a priority for initial Android port

---

### Q13: Test Device Details

**User's device:** Samsung Galaxy Tab S6 Lite 2024 on LineageOS 23.1, Android 16

**This is actually excellent for testing because:**
1. **S-Pen** - WACOM EMR technology, same as your desktop tablet
2. **LineageOS** - Clean Android without Samsung bloat
3. **Android 16** - Very new (API 36!), supports all modern stylus APIs
4. **"Poor performance"** - Perfect stress test for optimization!

**Note:** Android 16 is API level 36, which is ABOVE our target API 31. This means `requestUnbufferedDispatch()` will definitely work on your device.

**Interesting fact:** You mentioned the S-Pen works on your Wacom One tablet. This is because Samsung uses WACOM EMR technology in their tablets, so they're electrically compatible!

**Decision:** ✅ **Samsung Galaxy Tab S6 Lite 2024** as primary test device

---

## Additional Questions

### Q14: How to handle MuPDF cross-compilation?

MuPDF needs to be cross-compiled for Android. Options:

| Approach | Complexity | Notes |
|----------|------------|-------|
| **A) Pre-built binaries** | Low | If available (check MuPDF releases) |
| **B) Conan recipe** | Medium | Conan might have Android MuPDF recipe |
| **C) Manual CMake cross-compile** | Medium-High | Similar to Poppler approach |
| **D) Android AAR from Artifex** | Low | MuPDF provides official Android SDK |

**Research needed:** Check if MuPDF provides pre-built Android libraries or an AAR.

**Advice:** MuPDF actually provides an **official Android SDK** with pre-built libraries! See: https://mupdf.com/releases/

This could be MUCH simpler than the Poppler cross-compilation approach.

**Research Results:**

MuPDF provides several integration options for Android:

| Option | Description | Complexity |
|--------|-------------|------------|
| **mupdf-android-viewer** | Complete Android PDF viewer app (GitHub) | Reference implementation |
| **JNI bindings** | Java/Kotlin wrappers for native MuPDF | Medium |
| **Native CMake build** | Cross-compile libmupdf.so for Android NDK | Medium-High |

Key resources:
- GitHub: `ArtifexSoftware/mupdf-android-viewer` - Official Android viewer example
- Docs: `mupdf.readthedocs.io/en/latest/guide/using-with-java.html` - Java/JNI integration

**Best approach for SpeedyNote:**
Since we need C++ integration (not Java), we should:
1. Cross-compile MuPDF with Android NDK (similar to what community did with Poppler)
2. Create `MuPdfProvider` that uses MuPDF's C API directly
3. Link `libmupdf.a` or `libmupdf.so` into the Qt application

This is actually **simpler than Poppler** because:
- MuPDF has fewer dependencies (no fontconfig, cairo, etc.)
- MuPDF is designed for embedded systems
- Smaller binary size

**Decision:** ✅ **Cross-compile MuPDF with Android NDK** (simpler than Poppler)

---

### Q14.1: PdfProvider Method Coverage Analysis

The `PdfProvider` interface is compact (14 virtual methods). Here's MuPDF coverage:

| Method | MuPDF Support | Notes |
|--------|---------------|-------|
| `isValid()` | ✅ | `fz_open_document()` returns null on failure |
| `isLocked()` | ✅ | `pdf_needs_password()` |
| `pageCount()` | ✅ | `fz_count_pages()` |
| `title()` | ✅ | `pdf_lookup_metadata()` with "info:Title" |
| `author()` | ✅ | `pdf_lookup_metadata()` with "info:Author" |
| `subject()` | ✅ | `pdf_lookup_metadata()` with "info:Subject" |
| `filePath()` | ✅ | Store in wrapper class |
| `outline()` | ✅ | `fz_load_outline()` → recursive tree |
| `hasOutline()` | ✅ | Check if `fz_load_outline()` returns non-null |
| `pageSize()` | ✅ | `fz_bound_page()` returns `fz_rect` |
| `renderPageToImage()` | ✅ | `fz_new_pixmap_from_page()` → QImage |
| `textBoxes()` | ✅ | `fz_new_stext_page_from_page()` → iterate blocks |
| `supportsTextExtraction()` | ✅ | Return true |
| `links()` | ✅ | `fz_load_links()` → iterate link list |
| `supportsLinks()` | ✅ | Return true |

**Conclusion:** ✅ **MuPDF covers 100% of PdfProvider methods** - Full feature parity confirmed!

---

### Q15: Launcher UI on Android?

The current Launcher (`source/ui/launcher/`) was designed for desktop. Should we:

| Option | Description |
|--------|-------------|
| **A) Keep as-is** | Desktop launcher might work on tablets |
| **B) Android-specific launcher** | Native Android look and feel |
| **C) Simplified launcher** | Minimal changes, focus on core functionality |

**Advice:** The current Launcher uses Qt Widgets and should work on Android tablets. Since your Tab S6 Lite has a decent screen size (10.4"), the desktop UI should be usable. We can optimize later.

**Recommendation:** ✅ **Option A (keep as-is)** for initial port, optimize later if needed

---

### Q16: Navigation bar position on Android?

Android devices often have:
- Gesture navigation (swipe from bottom)
- Button navigation (back/home/recents at bottom)

The current NavigationBar is at the top. Should we adjust for Android?

**Advice:** Keep NavigationBar at top. Android's system navigation is handled by the OS at the screen edges, and Qt apps coexist with it fine. The top position is actually more Android-native (like Chrome, etc.).

**Decision:** ✅ **Keep NavigationBar at top** (matches Android conventions)

---

### Q17: Screen rotation handling?

Android tablets can rotate. Does DocumentViewport handle rotation well?

**Current behavior:** `resizeEvent()` already handles viewport resize and recenters content. This should work for rotation.

**Advice:** Should work out of the box. Test on device to confirm.

**Decision:** ✅ **Should work** (test on device)

From what I can see, screen rotations are already handled correctly on the Windows build on my 8-inch Raytrek Tab RT08WT. Both the launcher and the mainwindow work. 
---

### Q18: Touch gesture mode on Android?

The current TouchGestureHandler has three modes:
- Full gestures (2-finger pan/zoom)
- Y-axis only
- Disabled

On Android with stylus, what should the default be?

**Advice:** On Android with S-Pen:
- Stylus = drawing (pen tip)
- Finger = gestures (pan/zoom)

This is the **opposite** of the desktop default where mouse can be either.

**Recommendation:** Default to **Full gestures** on Android. When stylus is detected, automatically switch fingers to gesture mode.

**Decision:** ✅ **Full gestures default** on Android

I already designed the app with tablet PC support in mind. Most testing have been done on a low performance tablet PC (running Windows 10) by far. No changes to touch gestures. 

---

### Q19: Where to put MuPdfProvider code?

Options for file organization:

| Option | Structure |
|--------|-----------|
| **A) Same folder** | `source/pdf/MuPdfProvider.h/.cpp` alongside Poppler |
| **B) Separate folder** | `source/pdf/mupdf/MuPdfProvider.h/.cpp` |
| **C) Platform folder** | `source/android/MuPdfProvider.h/.cpp` |

**Advice:** Option A is cleanest. Both implement `PdfProvider`, so they belong together. Use `#ifdef Q_OS_ANDROID` for conditional compilation.

**Decision:** ✅ **Option A** - `source/pdf/MuPdfProvider.h/.cpp`
I agree with Option A. 
---

### Q20: Initial Android port scope - what's the MVP?

What features are required for the first working Android build?

**Proposed MVP (Minimum Viable Product):**

| Feature | Priority | Notes |
|---------|----------|-------|
| DocumentViewport rendering | ✅ Required | Core functionality |
| Stylus drawing (Pen/Marker) | ✅ Required | Primary use case |
| Eraser tool | ✅ Required | Basic editing |
| PDF loading (MuPDF) | ✅ Required | Key feature |
| Save/Load .snb | ✅ Required | Data persistence |
| Touch pan/zoom | ✅ Required | Navigation |
| Lasso selection | 🟡 Nice to have | Can defer |
| LayerPanel | 🟡 Nice to have | Can defer |
| OutlinePanel | 🟡 Nice to have | Can defer |
| Highlighter tool | 🟡 Nice to have | Needs text extraction |
| Markdown notes | ❌ Deferred | Not priority |
| Launcher | 🟡 Nice to have | Can use simple file picker initially |

**Recommendation:** Focus on core drawing + PDF + save/load first.

**User:** The MVP should really include ALL of them. Nothing requires special features or any modifications. Since they all work on tablet PCs, they should work on android mobile tablets as well. Getting code stubbed may consume more time than getting all of them working at once.

**Decision:** ✅ **Full feature parity from Day 1** - No reduced MVP, ship everything 

---

## Summary of Decisions

| Question | Decision | Status |
|----------|----------|--------|
| Q1: PDF backend | **MuPDF** (PdfRenderer lacks text extraction) | ✅ Decided |
| Q2: Build system | **Docker** (no Qt account, reproducible) | ✅ Decided |
| Q3: Min API level | **API 26** min, **API 31** target | ✅ Decided |
| Q4: Codebase | **New architecture only** (no InkCanvas) | ✅ Decided |
| Q9: Qt version | **Qt 6.7+** (better stylus support) | ✅ Decided |
| Q10: ABI support | **arm64-v8a only** (Qt6 = 64-bit only) | ✅ Decided |
| Q11: MuPDF license | **Compatible** (both AGPL) | ✅ Decided |
| Q12: Markdown | **Deferred** (not priority) | ✅ Decided |
| Q13: Test device | **Samsung Tab S6 Lite 2024** | ✅ Decided |
| Q14: MuPDF build | Cross-compile with NDK (simpler than Poppler) | ✅ Decided |
| Q15: Launcher UI | Keep as-is for initial port | ✅ Decided |
| Q16: NavBar position | Keep at top (Android convention) | ✅ Decided |
| Q17: Screen rotation | Should work (test on device) | ✅ Decided |
| Q18: Touch gestures | Full gestures default on Android | ✅ Decided |
| Q19: MuPdfProvider location | `source/pdf/MuPdfProvider.h/.cpp` | ✅ Decided |
| Q20: MVP scope | **Full feature parity** (no reduced MVP) | ✅ Decided |

---

## Next Steps

---

## Phase A: Environment Setup (Detailed)

### A.1: Create Docker Image

**Goal:** A Docker image containing Qt 6.7+ for Android arm64-v8a, Android SDK/NDK, and build tools.

#### A.1.1: Base Image Contents

The Docker image needs:

| Component | Version | Path in Container |
|-----------|---------|-------------------|
| Ubuntu | 22.04 LTS | Base image |
| Android SDK | Latest | `/opt/android-sdk` |
| Android NDK | r26 or r27 | `/opt/android-sdk/ndk/{version}` |
| Android Build Tools | 34.0.0+ | `/opt/android-sdk/build-tools/` |
| Android Platform | API 31+ | `/opt/android-sdk/platforms/` |
| Qt 6.7 (Android arm64) | 6.7.x | `/opt/qt/6.7/android_arm64_v8a` |
| Qt 6.7 (Linux host tools) | 6.7.x | `/opt/qt/6.7/gcc_64` |
| CMake | 3.22+ | System package |
| Ninja | Latest | System package |
| OpenJDK | 17 | System package |

#### A.1.2: Qt Installation Options

| Option | Pros | Cons |
|--------|------|------|
| **aqtinstall** | No Qt account, scriptable, CI-friendly | Unofficial, may break |
| **Qt Online Installer** | Official, reliable | Requires account, interactive |
| **Build from source** | Full control | Complex, time-consuming |

**Recommendation:** Use `aqtinstall` (Another Qt Installer) - a Python tool that downloads Qt without an account.

```bash
# Install aqtinstall
pip install aqtinstall

# Install Qt 6.7 for Android arm64
aqt install-qt linux android 6.9.3 android_arm64_v8a

# Install Qt 6.7 host tools (needed for moc, uic, etc.)
aqt install-qt linux desktop 6.9.3 gcc_64
```

#### A.1.3: Dockerfile Skeleton

```dockerfile
FROM ubuntu:22.04

# Install system dependencies
RUN apt-get update && apt-get install -y \
    cmake ninja-build git wget unzip \
    openjdk-17-jdk \
    python3 python3-pip \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install aqtinstall
RUN pip3 install aqtinstall

# Install Android SDK/NDK (using cmdline-tools)
ENV ANDROID_SDK_ROOT=/opt/android-sdk
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools && \
    wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip && \
    unzip -q commandlinetools-linux-*.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools && \
    mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest && \
    rm commandlinetools-linux-*.zip

# Accept licenses and install components
RUN yes | ${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager --licenses && \
    ${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager \
    "platforms;android-34" \
    "build-tools;34.0.0" \
    "ndk;26.1.10909125"

# Install Qt 6.7 for Android and host
ENV QT_VERSION=6.9.3
RUN aqt install-qt linux android ${QT_VERSION} android_arm64_v8a -O /opt/qt && \
    aqt install-qt linux desktop ${QT_VERSION} gcc_64 -O /opt/qt

# Set environment variables
ENV QT_ANDROID=/opt/qt/${QT_VERSION}/android_arm64_v8a
ENV QT_HOST=/opt/qt/${QT_VERSION}/gcc_64
ENV ANDROID_NDK_ROOT=${ANDROID_SDK_ROOT}/ndk/26.1.10909125

WORKDIR /workspace
CMD ["/bin/bash"]
```

#### A.1.4: Verification Test

Build a minimal "Hello Qt" app to verify the setup works:

```cpp
// test_main.cpp
#include <QApplication>
#include <QLabel>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QLabel label("Hello from Qt on Android!");
    label.show();
    return app.exec();
}
```

---

### A.2: Cross-Compile MuPDF

**Goal:** Build `libmupdf.a` (static library) for Android arm64-v8a.

#### A.2.1: MuPDF Dependencies

MuPDF is designed for embedded systems and has **minimal dependencies**:

| Dependency | Status |
|------------|--------|
| zlib | Usually bundled or system |
| libjpeg | Bundled (thirdparty/) |
| freetype | Bundled (thirdparty/) |
| harfbuzz | Bundled (thirdparty/) |
| OpenJPEG | Bundled (thirdparty/) |
| libpng | Bundled (thirdparty/) |

**Key advantage over Poppler:** MuPDF bundles its dependencies, making cross-compilation simpler.

#### A.2.2: MuPDF Android Build Script

```bash
#!/bin/bash
# build-mupdf-android.sh

set -e

MUPDF_VERSION="1.24.0"
NDK_ROOT="${ANDROID_NDK_ROOT}"
API_LEVEL=26
ARCH=arm64
INSTALL_PREFIX="/workspace/mupdf-android-sysroot"

# Clone MuPDF
git clone --depth 1 --branch ${MUPDF_VERSION} \
    https://github.com/ArtifexSoftware/mupdf.git

cd mupdf
git submodule update --init --recursive

# Configure for Android NDK
export CC="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${API_LEVEL}-clang"
export CXX="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${API_LEVEL}-clang++"
export AR="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar"
export RANLIB="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib"

# Build MuPDF (uses Makefile)
make \
    OS=android \
    build=release \
    HAVE_X11=no \
    HAVE_GLUT=no \
    HAVE_CURL=no \
    verbose=yes \
    -j$(nproc)

# Install headers and library
mkdir -p ${INSTALL_PREFIX}/lib
mkdir -p ${INSTALL_PREFIX}/include
cp build/release/libmupdf.a ${INSTALL_PREFIX}/lib/
cp build/release/libmupdf-third.a ${INSTALL_PREFIX}/lib/
cp -r include/* ${INSTALL_PREFIX}/include/

echo "MuPDF installed to ${INSTALL_PREFIX}"
```

#### A.2.3: Comparison with Poppler

| Aspect | MuPDF | Poppler |
|--------|-------|---------|
| Dependencies | ~6 (all bundled) | ~12 (need separate builds) |
| Build system | Makefile | CMake |
| Build time | ~5 minutes | ~30+ minutes |
| Binary size | ~5 MB | ~15 MB |
| Cross-compile difficulty | Low | High |

---

### A.3: Test Minimal Qt App on Device

**Goal:** Verify the entire toolchain works by running a simple app on the Samsung Tab S6 Lite.

#### A.3.1: Test App Structure

```
test-android-app/
├── CMakeLists.txt
├── main.cpp
└── android/
    └── AndroidManifest.xml
```

#### A.3.2: CMakeLists.txt for Android

```cmake
cmake_minimum_required(VERSION 3.22)
project(TestAndroidApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(TestAndroidApp main.cpp)

target_link_libraries(TestAndroidApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Android-specific properties
set_target_properties(TestAndroidApp PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
)
```

#### A.3.3: Build and Deploy Commands

```bash
# Configure
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DCMAKE_PREFIX_PATH=${QT_ANDROID} \
    -DQT_HOST_PATH=${QT_HOST} \
    -GNinja

# Build
cmake --build build

# Create APK
cmake --build build --target apk

# Install on device (via USB/ADB)
adb install build/android-build/TestAndroidApp.apk
```

#### A.3.4: Success Criteria

| Test | Expected Result |
|------|-----------------|
| APK builds without errors | ✅ |
| APK installs on Tab S6 Lite | ✅ |
| App launches and shows UI | ✅ |
| Touch input works | ✅ |
| App doesn't crash on rotation | ✅ |

---

### Phase A Timeline

| Task | Estimated Time | Dependencies |
|------|----------------|--------------|
| A.1.1-A.1.3: Write Dockerfile | 2-3 hours | None |
| A.1.4: Build Docker image | 30-60 min | Dockerfile |
| A.2: Cross-compile MuPDF | 1-2 hours | Docker image |
| A.3: Build test app | 1-2 hours | Docker + MuPDF |
| A.3: Test on device | 1 hour | USB cable, ADB |

**Total: ~1 day of focused work**

---

### Phase A Risks & Mitigations

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| aqtinstall fails to download Qt | Medium | Fallback: use official Qt installer in a separate step |
| MuPDF build fails on Android NDK | Low | MuPDF is battle-tested on Android; check build flags |
| APK won't install on LineageOS | Low | Enable developer mode, check ADB connection |
| Qt version mismatch issues | Medium | Pin exact versions in Dockerfile |

---

### Phase A Deliverables

1. [x] `android/Dockerfile` - Reproducible build environment ✅
2. [x] `android/docker-build.sh` - Build the Docker image ✅
3. [x] `android/docker-shell.sh` - Enter the container ✅
4. [x] Docker image built successfully (2026-01-15) ✅
5. [x] `android/build-mupdf.sh` - MuPDF cross-compilation script ✅
6. [x] MuPDF 1.24.10 built for arm64-v8a (libmupdf.a 50MB + libmupdf-third.a 9MB) ✅
7. [x] `android/test-app/` - Minimal test app + build script ✅
8. [x] Test app running on Tab S6 Lite - Qt 6.9.3 on Android 16 ✅
9. [x] **Phase A COMPLETE** (2026-01-16) ✅

---

### Q21-Q23: Setup Confirmation

**Q21:** Where to store Docker/build files?
- **Decision:** ✅ Option C - `android/` folder in SpeedyNote repo

**Q22:** Docker installed and working?
- **User:** Yes, latest version on Kubuntu

**Q23:** Tab S6 Lite set up for USB debugging?
- **User:** Yes

---

### Phase B: MuPdfProvider Implementation ✅ COMPLETE
4. [x] Create `MuPdfProvider.h/.cpp` implementing all 14 `PdfProvider` methods ✅
5. [x] Add `#ifdef Q_OS_ANDROID` conditional compilation in factory ✅
6. MuPDF features implemented:
   - Document loading/validation
   - Page count, size, metadata (title/author/subject)
   - Page rendering to QImage (with proper BGRA conversion)
   - Text extraction (word-by-word with bounding boxes)
   - Link extraction (internal goto + external URI)
   - Outline/TOC extraction (recursive)

### Phase C: Android CMake & Build ✅ COMPLETE
7. [x] Add Android section to `CMakeLists.txt` ✅
8. [x] Configure MuPDF vs Poppler selection (automatic via ANDROID check) ✅
9. [x] Platform-specific PDF_SOURCES in CMakeLists.txt ✅

### Phase D: Full App Build (IN PROGRESS)
10. [x] Create `android/build-speedynote.sh` build script ✅
11. [x] Create `android/app-resources/AndroidManifest.xml` ✅
12. [x] Update CMakeLists.txt with Android platform section ✅
13. [x] Add `qt_add_executable()` for Android builds ✅
14. [ ] Build APK with ALL features
15. [ ] Test on Samsung Tab S6 Lite
16. [ ] Verify: rendering, stylus, touch gestures, save/load

### Phase E: Stylus Optimization
13. [ ] Implement `requestUnbufferedDispatch()` for high-rate stylus (API 31+)
14. [ ] Benchmark stylus poll rate (target: 120Hz+)
15. [ ] Compare with desktop Wacom One performance

---

## References

- [Android PdfRenderer docs](https://developer.android.com/reference/android/graphics/pdf/PdfRenderer)
- [MuPDF documentation](https://mupdf.com/docs/)
- [Qt for Android docs](https://doc.qt.io/qt-6/android.html)
- [Android stylus best practices](https://developer.android.com/develop/ui/views/touch-and-input/stylus-input)
- Community Android port: `SpeedyNote-Android-android-conan/`

