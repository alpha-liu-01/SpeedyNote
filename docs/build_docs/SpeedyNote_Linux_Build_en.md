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
sudo apt install qt6-base-dev qt6-tools-dev libqt6concurrent6 libqt6xml6 libqt6network6

# PDF viewing (Poppler)
sudo apt install libpoppler-qt6-dev

# PDF export (MuPDF and dependencies)
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev

```

#### Dependency Summary

| Component | Packages | Purpose |
|-----------|----------|---------|
| **Build tools** | `build-essential cmake pkg-config` | Compilation |
| **Qt6** | `qt6-base-dev qt6-tools-dev` | UI framework |
| **Poppler** | `libpoppler-qt6-dev` | PDF viewing |
| **MuPDF** | `libmupdf-dev` | PDF export |
| **MuPDF deps** | `libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev` | MuPDF dependencies |
| **SDL2** | `libsdl2-dev` | Game controller support (optional) |

> **Note:** If MuPDF or its dependencies are not installed, PDF export will be disabled. The app will still function normally for viewing and annotating PDFs.

---

### Build

```bash
# Clone the repository
git clone https://github.com/alpha-liu-01/SpeedyNote.git
cd SpeedyNote

# Build SpeedyNote
./compile.sh

# Run
cd build && ./NoteApp
```

#### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_CONTROLLER_SUPPORT` | OFF | Enable SDL2 game controller support |
| `ENABLE_DEBUG_OUTPUT` | OFF | Enable verbose debug output |

Example with options:
```bash
cmake .. -DENABLE_CONTROLLER_SUPPORT=ON -DENABLE_DEBUG_OUTPUT=ON
```

---

### Install (Optional)

```bash
sudo make install
```

This installs:
- `NoteApp` binary to `/usr/local/bin/`
- Translations to `/usr/local/share/speedynote/translations/`
- Icon to `/usr/local/share/icons/hicolor/256x256/apps/`

---

### Troubleshooting

#### MuPDF not found

**Message:** `⚠️ MuPDF not found - PDF export will be disabled`

**Fix:** Install MuPDF and its dependencies:
```bash
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev
```

#### Poppler not found

**Error:** `Package 'poppler-qt6' not found`

**Fix:** Install Poppler Qt6 bindings:
```bash
sudo apt install libpoppler-qt6-dev
```

#### Qt6 not found

**Error:** `Could not find a package configuration file provided by "Qt6"`

**Fix:** Install Qt6 development packages:
```bash
sudo apt install qt6-base-dev qt6-tools-dev
```

---

### Platform Notes

#### ARM64 (Raspberry Pi, Apple Silicon via Linux VM)

The build system automatically detects ARM64 and applies appropriate optimizations. Use the same commands as x86_64.

#### Fedora / RHEL-based

Package names differ slightly:
```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel poppler-qt6-devel mupdf-devel harfbuzz-devel freetype-devel libjpeg-turbo-devel openjpeg2-devel gumbo-parser-devel mujs-devel
```

#### Arch Linux

```bash
sudo pacman -S cmake qt6-base qt6-tools poppler-qt6 mupdf harfbuzz freetype2 libjpeg-turbo openjpeg2 gumbo-parser mujs
```
#### Alpine Linux and postmarketOS

Note that the Alpine Linux version doesn't rely on Poppler. mupdf handles both PDF rendering and export. 

```bash
sudo apk add build-base cmake abuild qt6-qtbase-dev qt6-qttools-dev qt6-qtdeclarative-dev mupdf-dev
./compile.sh
./build-alpine-arm64.sh
```

---

### See Also

- [Windows Build Guide](SpeedyNote_Windows_Build_en.md)
- [macOS Build Guide](SpeedyNote_Darwin_Build_en.md)
- [Android Build Guide](ANDROID_BUILD_GUIDE.md)

