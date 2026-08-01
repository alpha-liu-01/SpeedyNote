# SpeedyNote Linux Build

### Preparation

- Ubuntu 22.04+ or other Debian-based distribution (x86_64 or ARM64)
- CMake 3.16+
- Qt 6.4+
- GCC or Clang compiler

---

### Dependencies

Install the required packages:

```bash
# Build essentials
sudo apt install build-essential cmake pkg-config

# Qt6
sudo apt install qt6-base-dev qt6-tools-dev qt6-svg-dev libqt6concurrent6 libqt6xml6 libqt6network6


# PDF (MuPDF and dependencies)
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev
```

#### Dependency Summary

| Component       | Packages                                                                                | Purpose                            |
| --------------- | --------------------------------------------------------------------------------------- | ---------------------------------- |
| **Build tools** | `build-essential cmake pkg-config`                                                      | Compilation                        |
| **Qt6**         | `qt6-base-dev qt6-tools-dev`                                                            | UI framework                       |
| **MuPDF**       | `libmupdf-dev`                                                                          | PDF viewing and export             |
| **MuPDF deps**  | `libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev` | MuPDF dependencies                 |
| **OCR**         | `curl tar coreutils` (+ vendored ONNX Runtime & models, see below)                      | Handwriting OCR (optional)         |

---

### Build

```bash
# Clone the repository
git clone https://github.com/alpha-liu-01/SpeedyNote.git
cd SpeedyNote

# Build SpeedyNote
./compile.sh

# Run
cd build && ./speedynote
```

#### Build Options

| Option                | Default | Description                 |
| --------------------- | ------- | --------------------------- |
| `ENABLE_DEBUG_OUTPUT` | OFF     | Enable verbose debug output |

Example with options:

```bash
cmake .. -DENABLE_DEBUG_OUTPUT=ON
```

---

### Handwriting OCR (optional)

SpeedyNote can recognize handwritten notes locally and on-device for text search
and selection. On Linux this uses **PaddleOCR (PP-OCRv5) via a vendored ONNX
Runtime** (CPU only — no network or GPU required at runtime). It is **optional**:
a build without it links and runs fine, with the OCR button simply grayed out.

Before building, fetch the vendored ONNX Runtime and the recognition models into
`linux/`:

```bash
# ONNX Runtime (CPU). Auto-detects the host arch (x86_64 or ARM64/aarch64).
./linux/fetch-onnxruntime.sh
# Override only when cross-fetching for another arch:
# ORT_ARCH=aarch64 ./linux/fetch-onnxruntime.sh   # force ARM64
# ORT_ARCH=x64     ./linux/fetch-onnxruntime.sh   # force x86_64

# PP-OCRv5 mobile recognition models (~tens of MB total)
./linux/fetch-ocr-models.sh

# Then build as usual — CMake auto-detects the vendored files and enables OCR.
./compile.sh
```

At configure time CMake auto-enables `SPEEDYNOTE_ENABLE_PADDLE_OCR` when both the
vendored ONNX Runtime and the default `latin_rec.onnx` model are present under
`linux/`; otherwise it prints a warning and OCR stays disabled.

| Model file        | Languages                                       |
| ----------------- | ----------------------------------------------- |
| `latin_rec.onnx`  | English + Latin scripts (default, mandatory)    |
| `ch_rec.onnx`     | Chinese + English + Japanese + Traditional      |
| `korean_rec.onnx` | Korean                                          |

The character dictionaries are embedded inside each `.onnx` file, so no separate
dictionary files are needed. The fetch scripts need `curl`, `tar`, and
`sha256sum`.

> **Packaging note:** `./build-package.sh` (and the release CI) runs these fetch
> scripts automatically and bundles `libonnxruntime.so*` plus the models into the
> `.deb`/`.rpm`/Arch packages and the Flatpak, so installed packages ship with
> OCR enabled. **Alpine/musl is the exception** — the prebuilt ONNX Runtime is a
> glibc binary, so OCR is intentionally disabled for `.apk` builds.

---

### Install (Optional)

```bash
./build-package.sh
sudo dpkg -i speedynote.deb
```

---

### Troubleshooting

#### MuPDF not found

**Message:** `⚠️ MuPDF not found - PDF export will be disabled`

**Fix:** Install MuPDF and its dependencies:

```bash
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev
```

#### Qt6 not found

**Error:** `Could not find a package configuration file provided by "Qt6"`

**Fix:** Install Qt6 development packages:

```bash
sudo apt install qt6-base-dev qt6-tools-dev
```

#### OCR button is grayed out / OCR unavailable

**Message (at configure time):** `PaddleOCR raster OCR: DISABLED -- vendored ONNX Runtime or default model not found under linux/`

**Fix:** Fetch the vendored ONNX Runtime and models, then reconfigure (see
[Handwriting OCR](#handwriting-ocr-optional)):

```bash
./linux/fetch-onnxruntime.sh
./linux/fetch-ocr-models.sh
rm -rf build && ./compile.sh   # reconfigure so CMake re-detects the files
```

#### Link fails with `libonnxruntime.so: file in wrong format`

**Cause:** The vendored ONNX Runtime under `linux/onnxruntime-build/` is for a
different CPU architecture than the machine you're building on (e.g. an x86_64
runtime fetched by hand, then reused on an ARM64/aarch64 box). The linker refuses
to combine it with the ARM64 objects.

**Fix:** Delete the stale runtime and rebuild — the fetch scripts auto-detect the
host architecture:

```bash
rm -rf linux/onnxruntime-build build
./build-package.sh -deb        # or ./compile.sh
```

---

### Platform Notes

#### ARM64 (Raspberry Pi, Apple Silicon via Linux VM)

The build system automatically detects ARM64 and applies appropriate optimizations. Use the same commands as x86_64.

#### Fedora / RHEL-based

Package names differ slightly:

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel qt6-qtsvg-devel mupdf-devel harfbuzz-devel freetype-devel libjpeg-turbo-devel openjpeg2-devel gumbo-parser-devel mujs-devel
```

#### Arch Linux

```bash
sudo pacman -S cmake qt6-base qt6-tools qt6-svg mupdf harfbuzz freetype2 libjpeg-turbo openjpeg2 gumbo-parser mujs
```

#### Alpine Linux and postmarketOS

```bash
sudo apk add build-base cmake abuild qt6-qtbase-dev qt6-qttools-dev qt6-qtsvg-dev qt6-qtdeclarative-dev mupdf-dev
```

---

### See Also

- [Windows Build Guide](SpeedyNote_Windows_Build_en.md)
- [macOS Build Guide](SpeedyNote_Darwin_Build_en.md)
- [Android Build Guide](ANDROID_BUILD_GUIDE.md)
