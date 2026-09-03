# SpeedyNote macOS Build


### Preparation

- A Mac running macOS 14+ to build on, ARM64 or x86-64. The resulting app runs on macOS 12+.
- Xcode command-line tools (`xcode-select --install`).

---

### Environment

Qt is the only dependency you have to choose how to install. Either works:

- **Homebrew:** `brew install qt@6`
- **Qt Online Installer / aqtinstall:** install Qt 6.9.3 or newer for macOS, then point the
  build at it with `export SPEEDYNOTE_QT_PREFIX=~/Qt/6.9.3/macos`. This is what CI uses,
  since Homebrew's `qt@6` pulls in the umbrella `qt` formula (61 dependencies, including
  QtWebEngine and Node.js) and no longer has Intel bottles.

The remaining packages:

```zsh
brew install cmake pkg-config librsvg
```

`librsvg` provides `rsvg-convert`, which renders the SVG app icon into `AppIcon.icns`.
Without it the app bundle ends up with no icon.

Note there is no `brew install mupdf` any more. MuPDF is built from source as a static
library by `macos/build-mupdf.sh`, which `compile-mac.sh` runs automatically the first time
(10-20 minutes, then reused). Homebrew's MuPDF bottle and its dylib dependencies
(freetype, harfbuzz, jpeg-turbo, openjpeg, jbig2dec, gumbo, brotli) are built for whatever
macOS release the bottle was made on, so bundling them pinned the app to macOS 14. The
static build compiles all of them at `-mmacosx-version-min=12.0` instead.

### Build

Run `compile-mac.sh` to build SpeedyNote. The compiled binary lands in `build/`, and the
app bundle plus DMG are written to the project root.

```zsh
./compile-mac.sh            # interactive
./compile-mac.sh -d --no-cli --strict   # what CI runs: DMG, no CLI prompt, strict verify
```

`--strict` makes bundle verification fatal instead of advisory. Use it for anything you
intend to distribute: without it, an app that still references `/opt/homebrew`,
`/usr/local` or `/Users` only prints a warning, launches fine on your machine, and fails
on everyone else's.

`macdeployqt` bundles the Qt frameworks, so the finished app does not need Qt installed.

### Verifying a build is distributable

The CI workflow checks these mechanically, and they are worth repeating locally if you
change how anything gets bundled:

```zsh
vtool -show-build SpeedyNote.app/Contents/MacOS/speedynote   # expect: minos 12.0
otool -L SpeedyNote.app/Contents/MacOS/speedynote           # expect: no /opt/homebrew, /usr/local, /Users
ls SpeedyNote.app/Contents/Resources/AppIcon.icns
```

Every Mach-O file under `Contents/Frameworks/` and `Contents/PlugIns/` should also report
a `minos` of 12.0 or lower. A higher number means something got bundled that raises the
app's minimum macOS version.

## Known issues

~~This build document may not reflect the building process of SpeedyNote on an arm64-based Mac. The dmg file offered on GitHub is x86-64 only, so it may not work on arm-based machines. For Apple Silicon Mac users, I highly recommend you compile an arm64 native binary, and the steps should be similar if not identical.~~ SpeedyNote is confirmed to be able to build correctly on arm64 Macs.

Builds are per-architecture, not universal: an arm64 Mac produces an arm64 app and an
Intel Mac produces an x86_64 app. GitHub Releases carry both.
