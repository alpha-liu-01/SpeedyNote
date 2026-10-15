# Windows capacitive pen add-on

This optional, experimental Qt generic plugin adds touch inking and whole-image
annotation to an **unmodified Windows x64 SpeedyNote 1.6.3 installation using
Qt 6.11.2**. It is provided separately from the main application build. Other
SpeedyNote releases and platforms have not been validated. The added controls
currently have Simplified Chinese labels.

## Behavior

- **Touch ink:** a single touchscreen contact draws with a passive capacitive
  pen or finger. A second contact ends the stroke and blocks further inking
  until all contacts lift. Real mouse and active stylus events retain their
  native handling; dialogs and text editors retain ordinary touch behavior.
- **手势 (Gestures):** switches between touch writing and native one-finger
  pan/two-finger zoom. The choice persists across launches.
- **笔锋 (Nib):** optionally simulates pen pressure from speed and direction,
  with start/end taper. This is software estimation, not hardware pressure.
- **圆润 (Smoothing):** optionally smooths new touch pen/marker trajectories
  using a continuous spline, reducing arc creases and small input jitter.
  It preserves pronounced corners and flushes the endpoint when the pen lifts.
- **Home → ＋ → 导入图片 (Import image):** makes the selected image a complete
  annotation page, with the pen selected. The image is a fixed background,
  separate from ink, so object tools and stroke undo cannot move or delete it.
  Canceling the picker leaves the home screen unchanged.
- **插入图片 (Insert image) inside a notebook:** provides a file picker,
  clipboard-image paste and single-file drag/drop entry points to the existing
  native image-object system, including move/resize, undo/redo and asset storage.
  These are conveniences for image objects already supported by SpeedyNote.

Home import decodes the image with orientation applied, then writes a lossless,
margin-free one-page PDF inside a new native `.snb` directory bundle. The page
bounds match the oriented image dimensions at 96 logical DPI. Native PDF
background rendering supplies zoom and independent ink layers. Both the
background and annotations survive save/reopen and moving the complete bundle;
the source image is read only. Store/copy the **entire `.snb` directory**, or use
the application's native notebook export. Transparent pixels are flattened
against a white page. Imports are limited to 64 MiB and formats available from
the installed Qt image plugins; this is one image per notebook, not a multipage
image import.

## Build and use

Prerequisites: PowerShell, a MinGW-w64 `g++` with C++20, and Qt 6.11.2 development
headers, import libraries and `moc`, matching the installation's architecture
and ABI. Tests additionally need the matching Qt Core/Gui private headers
(`qeventpoint_p.h`). No SDK or runtime binaries are included here. The build
script understands MSYS2 `include/qt6` and Qt SDK `include` prefix layouts.
The validated local toolchain was GCC 15.1 with MSYS2 Qt 6.11.2.

1. Copy a complete SpeedyNote 1.6.3 installation into a separate edition folder.
2. Run from this directory, replacing the example paths with your own:

   ```powershell
   .\build.ps1 -QtRoot 'C:\Qt\6.11.2\mingw_64' `
       -Compiler 'C:\mingw64\bin\g++.exe' `
       -EditionDirectory 'C:\Apps\SpeedyNote-TouchInk' -RunTests
   ```

3. Start `touch-ink-launcher.exe` in that folder. The launcher enables the plugin
   for its child process. Set **手势：关** to write with a capacitive pen, or
   **手势：开** to pan/zoom. **笔锋** defaults off; **圆润** defaults on.
4. For whole-image annotation, return home and choose **＋ → 导入图片**, then
   write and save normally with `Ctrl+S`.

The build stages generated sources and test results under ignored `build/`,
installs `generic/speedynote_touchink.dll`, and builds a static launcher. If a
running edition has the DLL locked, a pending update is installed on the next
launch after the edition windows close. No process is terminated by the builder
or launcher. To launch the original behavior in the copied folder, run
`speedynote.exe` without the plugin environment variable.

## Isolation and compatibility limits

The plugin gives this edition a separate application data/cache namespace
(`SpeedyNote-TouchInk`) and process-local HKCU view under
`Software\SpeedyNoteTouchInk\Profile`. Its notebook index starts independently;
existing notebooks can be opened explicitly.

SpeedyNote 1.6.3 hardcodes one shared-memory/local-server name. To allow both
editions to run, the plugin changes only the matching read-only data string in
**its own process memory**, using `VirtualProtect`. It never writes executable
sections, another process or the on-disk application executable. Registry
redirection affects the entire edition process, including its libraries.

UI integration relies on 1.6.3 Qt meta-object names, signals, tool order and
floating-action-button geometry. There is no stable public plugin API for these
hooks. A future native integration would avoid that version coupling; this
directory does not wire the plugin into the cross-platform application build.

A passive capacitive pen and a finger produce indistinguishable touch events.
There is no added palm rejection. Smoothing introduces a live tip lag of about
half the latest touch segment and can change very small shapes; turn it off
when immediate raw input is preferable. Existing strokes are never rewritten.
Physical hardware latency, pressure feel and palm interaction still need user
testing. Extremely uneven input spacing can retain local curvature peaks.

## Validation

`build.ps1 -RunTests` runs enabled/disabled input regressions and 27 geometry
checks against the repository's native `VectorLayer` renderer. See
[VALIDATION.md](VALIDATION.md) for the actual-application image tests and their
reproduction commands. Generated notebooks, logs, binaries and machine-specific
reports are excluded from version control.

All add-on sources are licensed under GPL-3.0-or-later, consistent with the
repository's [license](../../LICENSE).
