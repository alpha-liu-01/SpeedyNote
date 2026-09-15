# SpeedyNote HarmonyOS Build Guide

**Document Version:** 1.0
**Date:** September 2026
**Status:** ✅ VERIFIED WORKING (2-in-1 emulator, API 23)

---

## Overview

This guide covers building SpeedyNote for HarmonyOS / OpenHarmony. The build produces an
unsigned `.hap` package that installs on an R&D-mode emulator as-is, and on retail hardware
once signed with a Huawei-issued certificate.

The whole flow runs from the command line. **DevEco Studio is never opened**: it is an IDE for
ArkTS projects and cannot build a Qt CMake project. It is installed only for its bundled JDK
and its Device Manager (emulator images). Qt generates its own DevEco project as a build
artifact and packaging is driven by the command-line `hvigor`.

### Architecture

- **Target:** HarmonyOS arm64-v8a (the only architecture that exists for this platform)
- **Form factors:** `tablet` and `2in1` — deliberately *not* `phone`, see [Device types](#device-types-must-exclude-phone)
- **PDF Backend:** MuPDF 1.24.10 (cross-compiled, statically linked, hidden visibility)
- **OCR Backend:** none yet — the OCR features are compiled out
- **CLI:** not available — Qt apps are shared modules here, not executables
- **UI Framework:** Qt 6.12.0 for HarmonyOS (`harmonyos_arm64_v8a`)
- **Minimum SDK:** API 23

---

## Prerequisites

### Host System

Verified on macOS (Apple silicon). A Linux host should work with the same layout, but the
`QT_HOST` and `JAVA_HOME` defaults in `harmony/build-hap.sh` assume macOS and would need
overriding.

| Requirement | Where it must live | Why the path matters |
|-------------|--------------------|----------------------|
| HarmonyOS Command Line Tools | `/opt/harmonyos/command-line-tools` | **Not configurable.** Qt's prebuilt CMake config has this path baked in from Qt's own build machine. |
| Qt 6.12.0 for HarmonyOS | `~/Qt/6.12.0/harmonyos_arm64_v8a` | Override with `QT_ROOT` / `QT_VERSION`. |
| Qt 6.12.0 for the host | `~/Qt/6.12.0/macos` | Supplies `harmonydeployqt`, which only ships with the *host* Qt. |
| `ohos-additional-packages` | `~/.local/opt/ohos/additional-packages` | Override with `OHOS_ADDITIONAL_PACKAGES`. Provides fontconfig, FreeType and ICU. |
| DevEco Studio | `/Applications/DevEco-Studio.app` | Bundled JDK (`Contents/jbr`) and emulator images. Override with `DEVECO_APP` or set `JAVA_HOME`. |

Qt for HarmonyOS is installed through the Qt Maintenance Tool (the HarmonyOS component under
Qt 6.12). `ohos-additional-packages` is a separate download referenced from the Qt wiki.

### Device

- A HarmonyOS **2-in-1** or **tablet** emulator created in DevEco Studio's Device Manager, or
- retail hardware, which additionally requires a signed HAP (see [Signing](#signing-and-sideloading))

Emulators are R&D-mode images, so they install unsigned HAPs without complaint.

---

## Quick Start

```bash
# One time: cross-compile MuPDF
./harmony/build-mupdf.sh

# Build, package, install and launch on the running emulator
./harmony/build-hap.sh --source . --build-dir harmony/build-speedynote --run
```

Sanity-check the toolchain independently of SpeedyNote by building Qt's own gallery example,
which is what `build-hap.sh` does when given no `--source`:

```bash
./harmony/build-hap.sh --run
```

---

## Detailed Build Instructions

### Phase 1: Cross-compile MuPDF

```bash
./harmony/build-mupdf.sh            # or --clean to rebuild from scratch
```

Downloads MuPDF 1.24.10, builds it against the OHOS clang toolchain, and installs to:

- `harmony/mupdf-build/lib/libmupdf.a`
- `harmony/mupdf-build/lib/libmupdf-third.a`
- `harmony/mupdf-build/include/mupdf/`

This only needs doing once; CMake fails with a pointed error if the libraries are missing.

Everything is compiled with `-fvisibility=hidden`, and the script patches FreeType's
`public-macros.h` on the way through. That is not cosmetic. Every Qt HAP bundles a *shared*
`libfreetype.so` which Qt imports 44 `FT_*` symbols from, and it loads before
`libspeedynote.so`. Because default-visibility symbols are preemptible even inside their own
shared object, a bundled MuPDF FreeType compiled normally would export the same names and
MuPDF's internal calls could bind to Qt's copy — two FreeType builds sharing one symbol set
and each other's state. FreeType annotates its public API with
`__attribute__((visibility("default")))` unconditionally on clang, which is why hiding it
requires the patch rather than just the compiler flag.

Unlike the iOS build, there is no HarfBuzz surgery: `libQt6Gui.so` for HarmonyOS has its
HarfBuzz statically linked and hidden, exporting and importing zero `hb_*` symbols, so
MuPDF's copy cannot collide with it.

### Phase 2: Build, package, install

```bash
./harmony/build-hap.sh --source . --build-dir harmony/build-speedynote --run
```

The script runs this pipeline:

| Step | Tool | What it does |
|------|------|--------------|
| 1 | `qt-cmake` | Configure against the OHOS toolchain (Ninja) |
| 2 | `cmake --build` | Produce `libspeedynote.so` |
| 3 | `harmonydeployqt` | Generate a DevEco project and stage Qt + third-party libraries |
| 4 | `fix-icu-sonames.py` | Patch the ICU soname mismatch (see below) |
| 5 | `inject-permission-request.py` | Add the runtime permission request to the generated ArkTS ability |
| 6 | `hvigorw assembleHap` | Package the HAP |
| 7 | `hdc install` / `aa start` | Install and launch |

**Options:**

| Option | Description |
|--------|-------------|
| `--source <dir>` | CMake project to build (default: Qt's `widgets/gallery` example) |
| `--build-dir <d>` | Build directory (default: `harmony/build-<name>`) |
| `--release` | Release build (default: Debug) |
| `--clean` | Remove the build directory first |
| `--install` | Install the HAP via `hdc` after packaging |
| `--run` | Install, then launch and tail the log |
| `--no-package` | Stop after `cmake --build` |
| `--verbose` | Pass `--verbose` to `harmonydeployqt` |

**Environment overrides:** `QT_VERSION`, `QT_ROOT`, `OHOS_CLT`, `OHOS_ADDITIONAL_PACKAGES`,
`DEVECO_APP`, `JAVA_HOME`.

The script exports three things that are not on the PATH by default and whose absence produces
unhelpful errors: `ninja` and `cmake` from inside the SDK, `node` bundled with the command line
tools, and `JAVA_HOME` (missing Java reports only *"Unable to locate a Java Runtime"*).

It also exports `QT_ADDITIONAL_PACKAGES_PREFIX_PATH`. Passing only `CMAKE_FIND_ROOT_PATH`, as
the Qt wiki suggests, satisfies the compiler but silently leaves fontconfig, FreeType and ICU
out of the package: Qt's toolchain file seeds that variable from the paths its own CI used and
then filters to the ones that exist locally, so the local prefix drops out and nothing is
staged.

### Phase 3: Install manually (optional)

```bash
hdc install -r <path>/entry-default-unsigned.hap
hdc shell aa start -a QAbility -b org.qtproject.example.speedynote
hdc shell hilog -x | grep -i speedynote        # logs
```

---

## Platform-Specific Configuration

### CMake

CMake reports `UNIX` for OHOS while `LINUX` is empty, so every "UNIX means Linux desktop"
branch in `CMakeLists.txt` carries an explicit `AND NOT OHOS`, and each `elseif(OHOS)` branch
is placed *before* the `UNIX` one. Without that ordering the Linux branch runs and demands Qt
DBus, pkg-config and a system MuPDF, none of which exist here.

`qt_add_executable()` is mandatory, not merely preferred: on HarmonyOS it emits the shared
module the generated ArkTS host loads, plus the `*-harmony-deployment-settings.json` that
`harmonydeployqt` consumes. A plain `add_executable()` would produce an ELF binary nothing ever
loads.

Linked platform library: `libohenvironment.so` (Core File Kit), for
`OH_Environment_GetUserDocumentDir()`.

### Device types stay at Qt's default

`module.json5`'s `deviceTypes` is deliberately left as Qt generates it — `phone`, `tablet`,
`2in1`. Restricting it,

```cmake
# Don't: crashes on a tablet, and fixes nothing
set_property(TARGET speedynote PROPERTY
    QT_HARMONYOS_MODULE_DEVICE_TYPES "tablet;2in1")
```

was an attempt to work around the platform's `window.restore()` failing for a bundle that
declares `phone` ([QTBUG-148467](https://bugreports.qt.io/browse/QTBUG-148467)), which matters
because Qt implements `hide()`/`show()` of a top-level window as the platform's
`minimize()`/`restore()`. It does not help: `deviceTypes` is an install-time filter and does not
change how the platform classifies the hardware, and on a tablet `restore()` is refused for a
different reason anyway (`WMSLayoutPc: Restore: This is not PC or PcAppInPad, not supported`).
The window switch avoids `restore()` altogether instead — see
[Windows and ability instances](#windows-and-ability-instances).

### Permissions

`ohos.permission.READ_WRITE_DOCUMENTS_DIRECTORY` is declared via the target's
`_qt_harmonyos_permissions` property. It is what makes saving work at all — the sandbox
otherwise refuses writes to the user's Documents folder, and `QFileDialog` will happily return
a path there that `QFile` then cannot open.

Being a user-granted permission, declaring it is not enough: it must also be requested at
runtime through `abilityAccessCtrl.requestPermissionsFromUser()`, which is ArkTS-only. That is
what `harmony/inject-permission-request.py` patches into the generated `QAbility.ets`.

### ICU sonames

Qt's prebuilt `libQt6Core.so` has `DT_NEEDED` entries for versioned ICU libraries
(`libicuuc.so.78`), while `ohos-additional-packages` ships unversioned ones (`libicuuc.so`).
The mismatch does not fail the build; it fails at load time, and the symptom is entirely
misleading: `libqohos.so` never loads, so the ArkTS side reports
`TypeError: Cannot read property handleAbilityStageOnCreate of undefined`.

`harmony/fix-icu-sonames.py` rewrites the `DT_NEEDED` entries and renames the staged
libraries to match.

---

## Known Platform Limitations

| Feature | Status | Reason |
|---------|--------|--------|
| CLI (`--export`, batch operations) | Not available | Qt apps are shared modules loaded by an ArkTS host, not standalone executables. The CLI sources are excluded from this build. |
| OCR | Not available | No engine ported yet. PaddleOCR would need cross-compiling; HarmonyOS Core Vision Kit is ArkTS-only and would need a NAPI bridge. |
| System notifications | Not implemented | Falls through to the no-op branch. Would need Notification Kit rather than `org.freedesktop.Notifications`. |
| Single-instance | Deliberately disabled | See below. |
| Atomic file writes | Weakened | See below. |
| Fullscreen | Not honoured | The platform ignores `showFullScreen()` for the ability's main window and its sub-windows alike. The nav-bar button changes Qt's window state and nothing on screen. |
| Dialog placement | Worked around | Dialogs used to open in the top-left corner with their titles behind the status bar. `HarmonyDialogCentring` in `Main.cpp` now centres them; see below before touching window geometry. |
| Stylus pressure/tilt | Untested | The emulator has no pen device; `uinput -S` injects generic touch events. Needs hardware. |

### Q_OS_LINUX is defined

Qt defines `Q_OS_LINUX` on HarmonyOS, so desktop-Linux code paths compile in unless explicitly
excluded. Guards need `&& !defined(Q_OS_HARMONY)`. This bit `SystemNotification.cpp` (DBus),
the CLI entry points, `ipcs`/`ipcrm` recovery, and the `SIGTERM`/`SIGINT` handlers.

### Single-instance is disabled

The desktop single-instance mechanism uses `QSharedMemory`, which is POSIX shared memory here.
When the ability framework kills the app, the segment is never `shm_unlink`ed, so the next
launch sees a live instance and exits silently — permanently. The desktop Linux recovery path
(`ipcs`/`ipcrm`) does not apply to POSIX shm and is unavailable anyway. `isInstanceRunning()`
and `setupSingleInstanceServer()` therefore treat HarmonyOS like Android and iOS.

### `.snb` bundles: pickers and `rename()`

Two separate problems, both about `.snb` being a *directory* bundle:

1. The native file picker creates a zero-byte *file* at the chosen path, so `mkpath()` then
   fails. `.snb` dialogs pass `QFileDialog::DontUseNativeDialog` on HarmonyOS to get Qt's
   widget dialog instead, and `Document::saveBundle()` removes a zero-byte regular file if it
   finds one.
2. The `sharefs` layer backing the user's folders denies `rename(2)`. `QSaveFile` commits by
   writing a temporary file and renaming it, so `commit()` fails and it deletes its temporary —
   leaving an empty bundle. `source/platform/BundleFile.h` is a shim that is `QSaveFile`
   everywhere else and a direct-writing `QFile` subclass on HarmonyOS. Crash-atomicity is lost
   on this platform.

### Windows and ability instances

Each top-level Qt window is backed by an ability instance, and starting one requires the app to
be in the foreground. If the app ever becomes window-less it leaves the foreground, and the
next attempt to open a window fails and takes the process down. A `QMessageBox` shown before
any other window does exactly that, which is why the session-restore prompt is parented to an
already-visible Launcher (the same treatment macOS needs).

That backing is also why Qt cannot switch between two top-level windows here the way it does
elsewhere. `hide()` reaches the platform's `minimize()`, which works, while `show()` reaches
`restore()`, which a tablet refuses; `hideAbility()`/`showAbility()` fail too (error
`16000067`); and `raise()` only orders windows *within* whichever instance is already in the
foreground, so it cannot bring a backgrounded one forward. Any switch that hid a window lost it
for good, and the app looked like it had vanished.

So the app keeps a single ability instance. The Launcher owns it — it is the first window and
outlives every MainWindow — and each MainWindow is tagged as a **sub-window** of it before its
first show, which keeps it inside the Launcher's stage rather than letting it become an instance
of its own. Both directions of the switch are then ordinary z-order changes, and the task
switcher shows one entry. `source/harmony/HarmonyWindowSwitch.{h,cpp}` holds all of it; the
header comment is the long-form explanation. It is compiled on every platform, because the
show/raise/geometry sequence is shared and only the hide is platform-dependent.

Three things to know before touching window code on this platform:

- The Launcher must never be hidden (`mustStayResident()`) — hiding it minimises the instance,
  and nothing can restore it.
- A MainWindow must be tagged before it is first shown, since Qt decides the view type when it
  creates the platform window. `MainWindow`'s constructor calls `adoptAsLauncherSubWindow()` so
  that no construction site can forget.
- A sub-window gets no status-bar inset of its own, and `availableGeometry()` reports the whole
  display, so a MainWindow is positioned from the Launcher's client rect (`targetGeometry()`).
  Maximise and fullscreen state is not copied between windows (`copiesWindowState()`) because
  the platform ignores both.

### Dialog placement

Every dialog used to open in the top-left corner with its title bar behind the status bar. Qt
believed they were centred, so nothing in the app could tell.

A position reaches this platform only for a window Qt considers deliberately placed — one with
`Qt::WA_Moved` set, which is what `QWidget::create()` checks before sending a position rather
than only a size. `QDialog` does centre itself, in `adjustPosition()`, but clears `WA_Moved`
afterwards ("not really an explicit position"), so nothing is ever submitted and the window
stays where the platform put it. `BatchExportDialog` was the one dialog that came up centred,
because it moves itself in its constructor and so still has `WA_Moved` set when the window is
created.

`HarmonyDialogCentring` in `source/Main.cpp` centres the rest. Placing a window that already
exists is harder than it sounds, and the two things it has to get right are worth knowing before
touching it:

- **The platform reads a submitted geometry as a frame rect where Qt means a client rect**, and
  reports the result back the same way round. A window therefore lands one frame margin above the
  position asked for and one margin taller than the size asked for, and the filter subtracts both
  in advance. Submitting the client rect unchanged is not a near miss: the size comes back a
  margin larger, that is a resize, the resize triggers another placement, and the dialog walks
  down the screen growing by the height of its title bar until it fills it.
- **The position has to be re-stated after the show.** A dialog that asked for less room than its
  layout needs is resized on the first layout pass afterwards, and the platform returns the
  window to the origin as it applies that size. The filter runs on `Move` and `Resize` as well as
  `Show` for that reason, and converges because it only ever submits a geometry the window does
  not already have.

`QWidget::move()` on a realised window is silently ignored here, so it is only useful before the
platform window exists. Menus and tooltips are unaffected by any of this — they are given an
explicit position when they open, and the filter skips anything with `WA_Moved` set in any case.

---

## Signing and Sideloading

The HAP produced here is **unsigned**. R&D-mode emulators install it as-is.

Retail HarmonyOS NEXT hardware requires a Huawei-issued debug certificate bound to the target
device's UDID, obtained through a Huawei developer account. Once installed, sideloaded apps are
permanent — unlike iOS, there is no seven-day expiry. Community tooling exists to automate the
per-device certificate dance.

---

## Troubleshooting

### `TypeError: Cannot read property handleAbilityStageOnCreate of undefined`

`libqohos.so` failed to load, almost always the ICU soname mismatch. Confirm
`fix-icu-sonames.py` ran, and check `hdc shell hilog -x | grep -i "dlopen\|cannot find"`.

### `Unable to locate a Java Runtime`

`JAVA_HOME` is not set and DevEco Studio is not where the script expects. Set `JAVA_HOME` or
`DEVECO_APP`.

### `harmonydeployqt: Failed to open input file`

A relative `--build-dir`. The deploy step runs from inside the build directory, so relative
paths resolve against the wrong place. The script converts it to an absolute path; if invoking
`harmonydeployqt` by hand, do the same.

### `no *-harmony-deployment-settings.json`

The project used `add_executable()` instead of `qt_add_executable()`.

### MuPDF not found

```
MuPDF for HarmonyOS not found. Build it with ./harmony/build-mupdf.sh
```

### App launches once, then never again

The single-instance lockout described above. Should not occur in current builds; if it
reappears, the symptom is `main()` returning 0 within milliseconds and no faultlog at all.

### Nothing installs: `no device connected`

Boot an emulator from DevEco's Device Manager. If `hdc list targets` prints `[Empty]` with an
emulator visibly running, check for a second `hdc` server (`HDC_SERVER_PORT`).

---

## Directory Structure

```
SpeedyNote/
├── harmony/
│   ├── build-hap.sh                # Full pipeline: configure → build → deploy → package → run
│   ├── build-mupdf.sh              # Cross-compile MuPDF for arm64-v8a
│   ├── fix-icu-sonames.py          # Patch DT_NEEDED entries + rename staged ICU libraries
│   ├── inject-permission-request.py# Add requestPermissionsFromUser() to generated QAbility.ets
│   ├── mupdf-build/                # Built MuPDF (generated)
│   ├── mupdf-src/                  # MuPDF source (generated)
│   └── build-speedynote/           # CMake build dir + generated DevEco project (generated)
├── source/
│   ├── harmony/
│   │   ├── HarmonyEnvironment.h    # User Documents directory via Core File Kit
│   │   └── HarmonyEnvironment.cpp
│   └── platform/
│       └── BundleFile.h            # QSaveFile shim (direct write on HarmonyOS)
└── CMakeLists.txt
```

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-09-15 | Initial HarmonyOS port: MuPDF backend, HAP packaging, sandbox/save fixes, window management |

---

## See Also

- [docs/private/HARMONYOS_PORT_FEASIBILITY.md](../private/HARMONYOS_PORT_FEASIBILITY.md) — porting notes, root-cause write-ups and open issues
- [Qt for HarmonyOS](https://doc.qt.io/qt-6/harmonyos.html)
- [MuPDF Documentation](https://mupdf.com/docs/)
