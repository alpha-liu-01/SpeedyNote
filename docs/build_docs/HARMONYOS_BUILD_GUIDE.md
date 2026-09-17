# SpeedyNote HarmonyOS Build Guide

**Document Version:** 1.3
**Date:** September 2026
**Status:** ⚠️ Verified working on a **tablet** emulator (API 23), and on **retail tablet
hardware** as of the first alpha: a MatePad Air running HarmonyOS 6 draws with a
pressure-sensitive M-Pencil. The **2-in-1** case, which 1.0 recorded as verified, has since
regressed — see
[The workarounds target one window mode](#the-workarounds-target-one-window-mode).

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
- **Form factors:** `phone`, `tablet` and `2in1` — Qt's default, which must not be narrowed, see [Device types](#device-types-stay-at-qts-default)
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

### The workarounds target one window mode

Almost everything in [Known Platform Limitations](#known-platform-limitations) below is a
workaround for **one** of the two window managers HarmonyOS can run an app under, and the
distinction is not the device type:

- **Handheld full screen**, on a tablet or phone that is not in PC mode. One undecorated window
  fills the screen, `minimize()` works and `restore()` is refused outright, so hiding a
  top-level window is a one-way door. This is the mode the port is currently built for.
- **PC-mode windowing**, on a 2-in-1 and on a handheld whose user has switched PC mode on.
  Windows are decorated, freely placed, resizable and independently minimised, and `restore()`
  works.

PC mode is a runtime setting, not a property of the hardware, so a tablet can be in either
mode. Qt reads it separately from the device type — the QPA polls the
`window_pcmode_switch_status` system parameter — which means the form factor a build installs
on tells you nothing about which of the two it will get.

Several of the current workarounds are wrong in PC mode rather than merely redundant: a
MainWindow kept as a sub-window of the Launcher's ability cannot be maximised, because the
platform's window-state calls address the instance's own main window and skip its sub-windows;
and a dialog stripped of `Qt::FramelessWindowHint` gives up a real title bar there rather than
the imaginary margin it gives up on a tablet.

**The state of play, in two commits.** `eb01ea3` is the last build verified on a tablet;
`f848738` is the last verified on a 2-in-1. Do not treat the second as a base to restore from:
it predates the revert described below and still restricts `deviceTypes`, so it carries the
manifest line that crashes tablets. Making one build serve both means branching on the window
mode at runtime, and nothing in the app does that yet. See the feasibility document's
"The workarounds are for one window mode" for the full account.

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

Everything below was observed in handheld full-screen mode; read
[The workarounds target one window mode](#the-workarounds-target-one-window-mode) before
assuming any of it applies on a 2-in-1 or in PC mode.

| Feature | Status | Reason |
|---------|--------|--------|
| CLI (`--export`, batch operations) | Not available | Qt apps are shared modules loaded by an ArkTS host, not standalone executables. The CLI sources are excluded from this build. |
| OCR | Not available | No engine ported yet. PaddleOCR would need cross-compiling; HarmonyOS Core Vision Kit is ArkTS-only and would need a NAPI bridge. |
| System notifications | Not implemented | Falls through to the no-op branch. Would need Notification Kit rather than `org.freedesktop.Notifications`. |
| Single-instance | Deliberately disabled | See below. |
| Atomic file writes | Weakened | See below. |
| Fullscreen | Not honoured | The platform ignores `showFullScreen()` for the ability's main window and its sub-windows alike. The nav-bar button changes Qt's window state and nothing on screen. |
| Dialog placement | Worked around | Dialogs used to open in the top-left corner with their titles behind the status bar. `HarmonyDialogCentring` in `Main.cpp` now centres them; see below before touching window geometry. |
| Modal z-order | Worked around | The platform lets a tap outside a modal dialog raise the window the dialog blocks over it, leaving the app unresponsive with the dialog buried. `HarmonyModalKeeper` in `Main.cpp` raises it back; see below. |
| Menu placement | Worked around | A menu opened as a sliver in the top-left corner and dismissed itself, unless the same `QMenu` object had been opened once before. Call sites use `execMenuAt()` from `source/ui/MenuPopup.cpp` instead of `QMenu::exec()`; see below. |
| Submenus | Not available, worked around | The platform closes the whole menu chain as soon as a second popup window opens, so a submenu cannot be opened at all — it takes its parent down with it, and from the outside the menu just vanishes on the tap. Keep menus flat, or pass the submenu to `flattenSubmenu()` from `source/ui/MenuPopup.cpp`; see below. |
| Dialog sizing | Worked around | Hardcoded `setMinimumSize()` on a dialog *replaces* the minimum its layout reports rather than raising it, and this platform's default font is 12pt against a desktop 9pt, so desktop-tuned sizes clipped content here. Use `source/ui/dialogs/DialogSizing.h`; see below. |
| Touch-drag scrolling | Worked around | A drag inside a `QScrollArea` viewport does not scroll it. `QScroller::grabGesture(viewport, QScroller::LeftMouseButtonGesture)` does; the `TouchGesture` variant never fires. Only the export dialog is fixed so far. |
| Stylus pressure | Works on hardware | Confirmed pressure-sensitive on a retail MatePad Air with an M-Pencil. Not reproducible on the emulator, which has no pen device: `uinput -S` injects generic touch events. |
| Stylus tilt / eraser end | Untested | Same hardware constraint; the alpha report covers pressure only. |

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
- **A window that does not exist yet has to be placed before its first show, and then left
  alone.** `QWidget::move()` after the platform window exists is re-read against the frame
  instead of the client area, so the rect comes back applied twice: a MainWindow asked for the
  Launcher's client rect at `y=39` landed at `y=-37`, two 37px title bars higher, with its
  toolbar under the status bar. `preserveWindowState()` has always moved before showing;
  `switchTo()` now matches it for a first show, and is unchanged for windows that already exist.
- **The Launcher's own rect is not trustworthy until the platform has sized it.** It reads as
  `QWidget`'s default 640x480 for the first moments of a cold start, and since every rect used
  during startup is measured against it, anything reading it too early gets a plausible wrong
  answer — the session prompt landed in the top-left quadrant about one cold start in four.
  `createLauncherForColdStart()` waits for the window to fill the display's width, bounded at
  500 ms because in PC mode it never will.

### Dialog placement

Every dialog used to open in the top-left corner with its title bar behind the status bar. Qt
believed they were centred, so nothing in the app could tell.

A position reaches this platform only for a window Qt considers deliberately placed — one with
`Qt::WA_Moved` set, which is what `QWidget::create()` checks before sending a position rather
than only a size. `QDialog` does centre itself, in `adjustPosition()`, but clears `WA_Moved`
afterwards ("not really an explicit position"), so nothing is ever submitted and the window
stays where the platform put it.

A dialog that moves itself in its constructor keeps `WA_Moved`, and that is worse rather than
better: it stops `QDialog::showEvent()` from calling `adjustPosition()` *and* it used to make
this filter stand aside, so nothing repositioned it at all. `BatchImportDialog`,
`SaveDocumentDialog` and `ExportResultsDialog` all do it, all with the same
`move(parent->geometry().center() - rect().center())` computed from a `rect()` the layout has
not filled in yet, and all sat in the corner because of it. Since no dialog in the app means to
open anywhere but centred, the filter now clears `WA_Moved` on `Show` and places all of them.

`HarmonyDialogCentring` in `source/Main.cpp` does the centring. Placing a window that already
exists is harder than it sounds, and the four things it has to get right are worth knowing
before touching it:

- **Only `QWindow::setGeometry()` moves a dialog.** `QWidget::move()`, `QWidget::resize()` and
  `QWidget::setGeometry()` all arrive as a size alone: the window is resized and left exactly
  where it was, whatever the timing — before the show, immediately after it, or a frame later
  from a queued call. A MainWindow looks like a counter-example, but `HarmonyWindowSwitch` only
  ever asks one to sit at the position it already has. The QPA sources give the rule behind this:
  `QOhosFloatingWindow::setGeometry()` submits the size unconditionally but guards the position
  with `if (!qt_window_private(window())->positionAutomatic)`, and that flag is only cleared by
  `QWindow::setPosition()`/`setGeometry()`. A position Qt considers automatic is never sent.
- **The dialog window has to be frameless.** With a frame the platform places the window one
  frame margin (37px, the title bar height) away from the rect submitted and — the part that
  actually hurts — the QPA goes on mapping touches from the rect it was given, so the dialog
  draws in one place and answers taps a title bar lower. The filter sets
  `Qt::FramelessWindowHint` on `QEvent::ChildAdded`, which is the last event to arrive before
  `QDialog` creates its window and so the last chance to change the flag without recreating it.
  Nothing is given up: the platform draws no decoration on these sub-windows, and that margin
  only ever existed in the geometry arithmetic.

  **It must be set with `overrideWindowFlags()`, not `setWindowFlag()`.** `ChildAdded` arrives
  from the constructor, and `setWindowFlag()` reparents the widget, which re-inherits the style,
  which delivers `StyleChange` to a half-built object. `QMessageBox` answers that by re-applying
  its icon through a member it has not assigned yet, and the app dies in `QLabel::setPixmap()` —
  in *every* message box, not just one. `overrideWindowFlags()` assigns the same field `create()
  ` reads without any of that, and is safe here precisely because there is no platform window
  yet for it to leave out of sync. `QEvent::Polish` is not an alternative: `create()` runs before
  `ensurePolished()` inside `QWidgetPrivate::setVisible()`.
- **The position has to be re-stated after the show.** A dialog that asked for less room than its
  layout needs is resized on the first layout pass afterwards, and the position has to be
  reasserted for the size it ends up with. The filter runs on `Move` and `Resize` as well as
  `Show` for that reason, and converges because it only ever submits a geometry the window does
  not already have.
- **A submission made from the `Show` handler does not stick, and Qt's cached geometry will not
  tell you.** At `Show` the window is not on screen yet; the platform answers with its own
  placement — the top-left corner — and that lands back in Qt as the window's geometry and wins.
  Dialogs that let `adjustPosition()` place them recover by themselves, because it moves them
  after mapping and the resulting `Move` brings the filter back. A dialog that placed itself gets
  no such second event, so the filter also queues one re-centre from `Show`. Do not try to detect
  this by comparing against `QWindow::geometry()`: that cache is what Qt last set, not where the
  window is, and `QWindow::setGeometry()` returns early on a rect it believes is current — so a
  cache that happens to hold the right answer locks the app out of ever submitting it.

The filter leaves menus and tooltips alone — they are given an explicit position when they open —
but menus turn out to have a related problem of their own, below.

### Dialog sizes: never hardcode a minimum

Placement was one half of the dialog work; size was the other, and it came back from the first
alpha tester as two dialogs with their contents crushed. The rule behind it is plain Qt, not a
platform quirk:

**`setMinimumSize()` on a window replaces the minimum its layout reports. It does not raise it.**
`QLayout::activate()` installs the layout's own minimum on a window, but only on the axes without
an explicit one, so a hardcoded pair substitutes constants for the layout's actual requirement.
When the constants are smaller — and they will be, because they were measured against a 9pt
desktop font while this platform defaults to 12pt — Qt resolves the shortfall by squeezing widgets
past their own minimums and clipping whatever is last in the layout. The batch import dialog ran
170px under its layout's minimum, which cost it its file list and half of a button row.

So, for any dialog that has to work here:

- **Give a preferred size, not a minimum.** `DialogSizing::openAtSize()` in
  `source/ui/dialogs/DialogSizing.h` takes one, grows it to what the layout asks for, caps it at
  the screen, and sets no minimum, leaving that to the layout.
- **Do not assume any size fits.** The export dialog's layout needs 966px on a 960px-tall display,
  so no window size can show it whole; its tab pages go through `DialogSizing::inScrollArea()`,
  which leaves the content's height alone but lets the *dialog* be smaller than its contents and
  scroll instead of crushing them. Wrap every page of a `QTabWidget`, not just the tall one — a
  stacked layout is as tall as its tallest page.
- **Do not put font sizes in stylesheets.** `font-size: 13px` is small print at 96dpi and level
  with the body text on this platform, which reports 72dpi against a 12pt default — that is what
  "the fonts are too large" turned out to mean. `DialogSizing::scaledFont(font(), 0.9)` stays
  proportional; the same goes for absolute emphasis like `setPointSize(16)`.
- **A scroll area needs `QScroller` to answer a drag** (see the limitations table), and its size
  hint is not a way to ask how tall its content is from a constructor: the hint is cached against
  the widget's pre-layout size, and came back 486px for content needing 966px.

### Menus open in the corner and dismiss themselves

`QMenuPrivate::popup()` creates the menu's window before it has worked out where the menu goes,
and this platform keeps a window at the rect it was created with: a geometry submitted between
`create()` and `show()` is dropped. A menu built where it is used is created with `QWidget`'s
default rect for a widget that has a parent, so what appears is a 100x30 sliver at 0,0.

It then closes within a tenth of a second, and the QPA sources say why: a node reporting
`externalContentClickDetected` runs `QOhosPlatformWindow::closeAllActivePopups()`, which sends a
close event to *every* visible `Qt::Popup` window in the app. A menu stranded in the corner is
nowhere near the tap that opened it, so that tap counts as content outside it and takes it
straight back down.

Only the *first* popup of a given `QMenu` object is affected. The second lands correctly, the
window existing by then and being moved rather than shown. That asymmetry is the whole reason the
overflow menu behind the nav bar's ⋮ button looked like it worked: it is kept in a member
variable, so only its first use in a run is lost, while the ＋ button's menu is rebuilt on every
click and so never worked at all.

`execMenuAt()` in `source/ui/MenuPopup.cpp` replaces `QMenu::exec()` at every call site. It
submits the geometry again through `QWindow::setGeometry()` from a queued call, once
`QMenu::popup()` has created the window, and holds the menu at zero opacity until then so the
corner is never where it first appears. The position has to be handed in from the call site,
because by the time the menu is visible nothing remembers where it was meant to go: Qt's own rect
and the platform's have both been overwritten with the creation rect.

Submenus are beyond this. They can be placed the same way — their position is derivable from the
parent menu's active item — but opening the second popup window is itself an outside interaction,
and `closeAllActivePopups()` closes every popup rather than the one that lost the interaction, so
parent and child go down together. Nothing in the app can prevent that, so anything that has to
work here belongs in a flat menu.

Note what that looks like from the outside, because it is not what a broken submenu usually looks
like: the parent closes too, so the entire menu disappears on the tap, which is indistinguishable
from having missed the item.

`flattenSubmenu()` in `source/ui/MenuPopup.cpp` is how the launcher's four nested menus — three
Export submenus and Move to Folder — are made reachable. Build the submenu as usual, then hand it
and its parent to `flattenSubmenu()`; on HarmonyOS its actions are lifted into the parent and the
item that opened it stays in place, disabled, as a heading for them. Off HarmonyOS it does nothing,
so the same call site keeps real submenus on desktop — which is why action labels need to read
correctly in both layouts (`To PDF...` under an `Export` heading).

### Modal dialogs are not kept in front

The platform does not enforce modality in the z-order — it has already downgraded
`Qt::ApplicationModal` to `Qt::WindowModal`. A tap outside an open dialog raises the window that
dialog blocks over the top of it: the dialog drops from `ZOrd` 104 to 103 and the fullscreen
MainWindow takes 104, keeping its geometry and its modality but disappearing from view. Qt's
modal event loop then discards every press that lands on the window now in front, so the app
looks frozen with no dialog to be seen and nothing wrong with either window.

Qt is told almost none of this, which is why `HarmonyModalKeeper` in `source/Main.cpp` works from
two events rather than one:

- `WindowDeactivate` on the dialog itself. The window that came forward is never activated —
  Qt's own modality is what blocks it — so the dialog's own deactivation is the whole report.
- `Hide` on any modal. A dialog closing over another one is reported to neither of them:
  dismissing the colour picker opened from the settings dialog leaves the settings dialog at
  whatever z-order a tap outside pushed it down to, without so much as a `WindowActivate`.

Both queue a raise of `QApplication::activeModalWidget()`. That is the *topmost* modal, so a
nested dialog activating itself is left alone instead of the two fighting over the z-order. The
raise is skipped while the app is in the background, because `raise()` on a sub-window goes to
the top of the whole app and would drag it back into view.

---

## Signing and Sideloading

The HAP produced here is **unsigned**. R&D-mode emulators install it as-is.

Retail HarmonyOS NEXT hardware requires a Huawei-issued debug certificate bound to the target
device's UDID, obtained through a Huawei developer account. Once installed, sideloaded apps are
permanent — unlike iOS, there is no seven-day expiry. Community tooling exists to automate the
per-device certificate dance.

Because the certificate is bound to one device, a prebuilt HAP cannot be signed on our side for
general distribution: each tester signs the same unsigned HAP for their own tablet, using their
own free developer account. That is the model
[HARMONYOS_ALPHA_TESTING.md](../HARMONYOS_ALPHA_TESTING.md) documents for testers, and it is also
what keeps the AGC device quota from becoming the bottleneck — see Q2 in the feasibility notes for
the quota figures.

Build the artifact testers receive with `--release`, and rename it from
`entry-default-unsigned.hap`. Note that `hvigorw assembleHap` always packages the `default`
product, so the filename says `-unsigned` either way; that is not a sign the flag was ignored.

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
| 1.3 | 2026-09-17 | First alpha report: stylus pressure confirmed on a MatePad Air; hardcoded dialog minimums replace the layout's own and clipped content at 12pt; submenus are flattened via `flattenSubmenu()` rather than left unreachable; a drag does not scroll a `QScrollArea` without `QScroller` |
| 1.2 | 2026-09-16 | Dialog and window placement: the frame-drop must use `overrideWindowFlags()` or every message box crashes; self-placing dialogs are the ones that opened in the corner, not the exception to it; a first show has to be placed before it, not after; the Launcher's rect is unusable early in a cold start |
| 1.1 | 2026-09-15 | Record the handheld/PC-mode split and the 2-in-1 regression; correct the form-factor summary, which still claimed `phone` was excluded |
| 1.0 | 2026-09-15 | Initial HarmonyOS port: MuPDF backend, HAP packaging, sandbox/save fixes, window management |

---

## See Also

- [docs/HARMONYOS_ALPHA_TESTING.md](../HARMONYOS_ALPHA_TESTING.md) — tester-facing install guide: self-signing, the storage permission, test priorities and known gaps
- [docs/private/HARMONYOS_PORT_FEASIBILITY.md](../private/HARMONYOS_PORT_FEASIBILITY.md) — porting notes, root-cause write-ups and open issues
- [Qt for HarmonyOS](https://doc.qt.io/qt-6/harmonyos.html)
- [MuPDF Documentation](https://mupdf.com/docs/)
