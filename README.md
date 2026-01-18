# 📝 SpeedyNote

<div align="center">

<img src="https://i.imgur.com/Q7HPQwK.png" width="200" alt="SpeedyNote Logo">

**A blazing-fast, cross-platform note-taking app for stylus users**

*Built for students who need iPad-quality annotation on budget hardware*

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20Android-brightgreen)]()
[![Qt](https://img.shields.io/badge/Qt-6.x-41CD52?logo=qt)]()

[English](#-features) • [中文](./docs/zh_Hans/readme_chn.md)

</div>

---

## Why SpeedyNote?

| The Problem | SpeedyNote's Solution |
|-------------|----------------------|
| 💸 OneNote doesn't support PDF annotation | ✅ Full PDF support with fast rendering |
| 🐌 Xournal++ is painfully slow on large PDFs | ✅ 360Hz input on a Celeron N4000 (1.1GHz) |
| 💰 GoodNotes/Notability cost $10+ and require iPad | ✅ Free & open source, runs on $50 tablets |
| 📱 Most note apps are mobile-only or desktop-only | ✅ Same experience on Windows, Linux, macOS & Android |

---

## ✨ Features

### 🚀 Performance First
- **360Hz stylus polling** on low-end hardware (tested: Celeron N4000 @ 1.1GHz)
- **Instant PDF loading** - large documents open in seconds, not minutes
- **Small memory footprint** - native C++ with no Electron bloat
- **ARM64 native builds** - optimized for Snapdragon laptops and Rockchip Chromebooks

### 🖊️ Professional Drawing Tools
- **Pressure-sensitive inking** with Pen, Marker, and Highlighter tools
- **Vector-based strokes** - zoom infinitely without pixelation
- **Multi-layer editing** (SAI2-style) - add, delete, reorder, merge layers
- **Stroke eraser** with full undo/redo support
- **Touch gestures** - two-finger pan, pinch-to-zoom, palm rejection

### 📄 Document Modes
- **Paged Notebooks** - traditional page-by-page notes (`.snb)
- **Edgeless Canvas** - infinite whiteboard with lazy-loading tiles (`.snb)
- **PDF Backgrounds** - annotate PDFs with clickable internal links
- **Sharing** - `.snbx` note bundles allows easy cross-platform note sharing. 

### 🎯 Tablet-First UX
- **Action bars** - context-sensitive buttons appear when you need them
- **Subtoolbars** - quick access to tool settings without menu diving
- **Page panel** - thumbnail navigation with drag-to-reorder
- **PDF outline** - click TOC entries to jump to sections

### 🔗 Advanced Features
- **Link objects** - create clickable links to markdown notes, URLs, or positions
- **Markdown notes** - attach rich text notes to any page or position
- **Multi-tab editing** - work on multiple documents simultaneously
- **Auto-save** - never lose work with automatic backup

---

## 📸 Screenshots

<!-- TODO: Replace with actual screenshots -->

| PDF Annotation | Layer Panel | Page Thumbnails |
|----------------|-------------|-----------------|
| ![PDF](https://i.imgur.com/xgmYhfK.png) | ![Layers](https://i.imgur.com/NelpAMv.png) | ![Pages](https://i.imgur.com/A93UeAT.png) |

| Edgeless Canvas | Action Bar | Subtoolbar |
|-----------------|------------|------------|
| ![Edgeless](https://i.imgur.com/wHLeyIj.png) | ![Action](https://i.imgur.com/wHLeyIj.png) | ![Subtoolbar](https://i.imgur.com/VSvZaxA.png) |

| Link Objects | Markdown Support | Android |
|-----------------|------------|------------|
| ![LinkObjects](https://i.imgur.com/QkEw57Y.png) | ![Markdown](https://i.imgur.com/yKVJw5E.png) | ![Android](https://i.imgur.com/rfAJMNF.png) |

---

## 🚀 Getting Started

### System Requirements

| Platform | Minimum | Recommended |
|----------|---------|-------------|
| **Windows** | Windows 10 1809 | Windows 11 |
| **macOS** | macOS 12 (to be tested) | macOS 15+ |
| **Linux** | Ubuntu 22.04 / Fedora 38 | Any with Qt 6.4+ |
| **Android** | Android 8.0 (API 26) | Android 13+ |

**Hardware:** Any x86_64 or ARM64 CPU. Tested on Intel Core i5 470UM (2010), Celeron N4000, Snapdragon 7c Gen 2, Rockchip RK3399

### 💾 Installation

#### Windows / macOS / Linux

Download the latest release from **[GitHub Releases](https://github.com/alpha-liu-01/SpeedyNote/releases)** or the official website.

| Platform | Package |
|----------|---------|
| Windows | `.exe` installer |
| macOS | `.dmg` disk image (to be offered) |
| Debian/Ubuntu | `.deb` package |
| Fedora/RHEL | `.rpm` package |
| Arch Linux | `.pkg.tar.zst` package |

#### Android

**Option 1: Google Play Store** (coming soon), supports development  
**Option 2: Build from source** - Free, see [Android Build Guide](./docs/build_docs/ANDROID_BUILD_GUIDE.md)

>  The Play Store version is a convenience fee. The source code is always free under GPL v3.

#### Linux APT Repository (Debian/Ubuntu)

```bash
# Add repository and install
wget -O- https://apt.speedynote.org/speedynote-release-key.gpg | \
  sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/speedynote.gpg

echo "deb [arch=amd64,arm64 signed-by=/etc/apt/trusted.gpg.d/speedynote.gpg] \
  https://apt.speedynote.org stable main" | \
  sudo tee /etc/apt/sources.list.d/speedynote.list

sudo apt update && sudo apt install speedynote
```

---

## 🛠️ Building From Source

### Prerequisites

| Platform | Requirements |
|----------|--------------|
| All | CMake 3.16+, C++17 compiler |
| Windows | MSYS2 with clang64/clangarm64 toolchain |
| macOS | Xcode Command Line Tools, Homebrew |
| Linux | Qt 6.4+ dev packages, Poppler-Qt6 |
| Android | Docker (see [Android Build Guide](./docs/build_docs/ANDROID_BUILD_GUIDE.md)) |

### Quick Build

```bash
# Clone the repository
git clone https://github.com/alpha-liu-01/SpeedyNote.git
cd SpeedyNote

# Windows (MSYS2 clang64 shell)
./compile.ps1

# macOS
./compile-mac.sh

# Linux
./compile.sh
# Or build packages: ./build-package.sh
```

### Detailed Build Guides

- [Windows Build Guide](./docs/build_docs/SpeedyNote_Windows_Build_en.md)
- [macOS Build Guide](./docs/build_docs/SpeedyNote_Darwin_Build_en.md)
- [Android Build Guide](./docs/build_docs/ANDROID_BUILD_GUIDE.md)


---

## 📁 File Formats

| Format | Description | Use Case |
|--------|-------------|----------|
| `.snb` | Bundle folder with tiles | Edgeless canvas, large projects |
| `.snbx` | Compressed bundle (ZIP) | Sharing, backup |

**Note:** The legacy `.spn` format from v0.x is not supported.

---

## 🌍 Supported Languages

SpeedyNote supports multiple languages:

- 🇺🇸 English
- 🇨🇳 简体中文 (Simplified Chinese)
- 🇪🇸 Español (Spanish) (partial)
- 🇫🇷 Français (French) (partial)

> Contributions for additional translations are welcome!

---

## 🤝 Contributing

Contributions are welcome! Please feel free to:

- 🐛 Report bugs via [GitHub Issues](https://github.com/alpha-liu-01/SpeedyNote/issues)
- 💡 Suggest features
- 🌍 Add translations
- 🔧 Submit pull requests

---

## 📜 License

SpeedyNote is licensed under the **GNU General Public License v3.0**.

- ✅ Free to use, modify, and distribute
- ✅ Source code always available
- ✅ Commercial use allowed (Play Store version)
- 📋 Derivative works must also be GPL v3

See [LICENSE](./LICENSE) for details.

### Third-Party Libraries

| Library | License | Usage |
|---------|---------|-------|
| Qt 6 | LGPL v3 | UI framework |
| Poppler | GPL v2/v3 | PDF rendering (desktop) |
| MuPDF | AGPL v3 | PDF rendering (Android) |
| QMarkdownTextEdit | MIT | Markdown editor |
| miniz | MIT | ZIP compression |

---

## 💖 Support the Project

If SpeedyNote helps you, consider:

- ☕ [Buy me a coffee](https://buymeacoffee.com/alphaliu01)
- ⭐ Starring this repository
- 📱 Purchasing the Android version on Google Play
- 🐛 Reporting bugs and suggesting improvements
- 🌍 Contributing translations

---

<div align="center">

**Made with ❤️ for students who deserve better tools**

*SpeedyNote v1.0*

</div>
