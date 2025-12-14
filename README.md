# 📝 SpeedyNote

<div align="center">
<img src="https://i.imgur.com/Q7HPQwK.png" width="300"></img>
</div>
<br></br>
<div align="center">
<a href="https://hellogithub.com/repository/alpha-liu-01/SpeedyNote" target="_blank"><img src="https://abroad.hellogithub.com/v1/widgets/recommend.svg?rid=e86680d007424ab59d68d5e787ad5c12&claim_uid=e5oCIWstjbEUv9D" alt="Featured｜HelloGitHub" style="width: 250px; height: 54px;" width="250" height="54" /></a>
</div>


---
_A lightweight, fast, and stylus-optimized note-taking app built for classic tablet PCs, low-resolution screens, and
vintage hardware._

如果您恰好不懂英文，请移步[中文README](./readme/zh_Hans.md)



![cover](https://i.imgur.com/4YMM2Fx.png)

---

## ✨ Features

- 🖊️ **Pressure-sensitive inking** with stylus support
- 📄 **Multi-page notebooks** with tabbed or scrollable page view
- 📌 **PDF & PowerPoint background integration** with annotation overlay
- 🌀 **Dial UI + Joy-Con support** for intuitive one-handed control
- 🎨 **Per-page background styles**: grid, lined, or blank (customizable)
- 💾 **Portable `.spn` notebooks** for note storage
- 🔎 **Zoom, pan, thickness, and color preset switching** via dial
- 🗔 **Markdown sticky notes are supported** for text-based notes
- 💡 **Designed for low-spec devices** (133Hz Sample Rate @ Intel Atom N450)
- 🌏 **Supports multiple languages across the globe** (Covers half the global population)

---

## 📸 Screenshots

| Drawing                                  | Dial UI / Joycon Controls               | Overlay Grid Options                     |
| ---------------------------------------- | --------------------------------------- | ---------------------------------------- |
| ![draw](https://i.imgur.com/iARL6Vo.gif) | ![pdf](https://i.imgur.com/NnrqOQQ.gif) | ![grid](https://i.imgur.com/YaEdx1p.gif) |

---

## 🚀 Getting Started

### ✅ Requirements

- Windows 7/8/10/11/macOS 12+/Ubuntu/Debian/Fedora/RedHat/ArchLinux/AlpineLinux
- Qt 5 or Qt 6 runtime (bundled in Windows releases)
- Stylus input (Wacom recommended)
- **LibreOffice** (optional, required for PowerPoint file support)

### 💾 Installation

---

#### Windows

Download the installer from [SpeedyNote official site](https://www.speedynote.org) 
Windows 7 or Windows 8 users may download the 32-bit legacy version instead.  

#### Linux

Download the `deb`,`rpm` `.pkg.tar.zst` or `.apk` packages from [SpeedyNote official site](https://www.speedynote.org) and install them locally, or

For Debian/Ubuntu users, you can get SpeedyNote from `apt.speedynote.org` and run
```bash
wget -O- https://apt.speedynote.org/speedynote-release-key.gpg | sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/speedynote.gpg && echo "deb [arch=amd64,arm64 signed-by=/etc/apt/trusted.gpg.d/speedynote.gpg] https://apt.speedynote.org stable main" | sudo tee /etc/apt/sources.list.d/speedynote.list && sudo apt update && sudo apt install speedynote
```
this command to install SpeedyNote to your computer, and update with `sudo apt update && sudo apt upgrade` commands. 

🛠️ Usage

1. Launch `SpeedyNote` shortcut on desktop
2. Click `Open PDF` to annotate a PDF document or click `New` to create a new notebook without loading a PDF.
3. Start writing/drawing using your stylus
4. Use the **MagicDial** or **Joy-Con** to change tools, zoom, scroll, or switch pages
5. Click the `×` on the tab and save the notebook as a `.spn` package

###### OR

1. Right click a PDF file in File Explorer (or equivalent)
2. Click open with and select SpeedyNote
3. Create an `spn` notebook package in the directory of the PDF file
4. Next time when you double click the `spn` notebook, all the notes with the PDF background will be loaded.
5. Start writing/drawing using your stylus


---

## 🎮 Controller Support

SpeedyNote supports controller input, ideal for tablet users:

- ✅ **Left Joy-Con supported**
- 🎛️ Analog stick → Dial control
- 🔘 Buttons can be mapped to:
  - Control the dial with multiple features
  - Toggle fullscreen
  - Change color / thickness
  - Open control panel
  - Create/delete pages

> Long press + turn = hold-and-turn mappings

---

## 📁 Building From Source

#### Windows

[Windows Build Documentation](./docs/SpeedyNote_Windows_Build_en.md)

#### Darwin
[Darwin Build Documentation](./docs/SpeedyNote_Darwin_Build_en.md)

#### Linux

1. run  `./build-package.sh`
2. Install the packages for your Linux distro.
   `.deb`, `rpm`, `.pkg.tar.zst` and `.apk` are tested and working.
