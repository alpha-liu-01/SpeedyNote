# SpeedyNote Linux 构建指南

### 准备

- Ubuntu 22.04+ 或者其它基于Debian的Linux发行版 (x86_64 或者 ARM64)
- CMake 3.16+
- Qt 6.4+
- GCC 或者 Clang 编译器

---

### Dependencies

Install the required packages:

```bash
# Build essentials
sudo apt install build-essential cmake pkg-config

# Qt6
sudo apt install qt6-base-dev qt6-tools-dev libqt6concurrent6 libqt6xml6 libqt6network6


# PDF (MuPDF and dependencies)
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev
```

#### Dependency Summary

| Component      | Packages                                                                                | Purpose   |
| -------------- | --------------------------------------------------------------------------------------- | --------- |
| **构建工具**       | `build-essential cmake pkg-config`                                                      | 编译        |
| **Qt6**        | `qt6-base-dev qt6-tools-dev`                                                            | UI框架      |
| **MuPDF**      | `libmupdf-dev`                                                                          | PDF 浏览和导出 |
| **MuPDF deps** | `libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev` | MuPDF 的依赖 |

### 构建

```bash
# 克隆这个仓库
git clone https://github.com/alpha-liu-01/SpeedyNote.git
cd SpeedyNote

# 构建SpeedyNote
./compile.sh

# 运行
cd build && ./speedynote
```

#### 构建选项

| 选项                          | Default | Description          |
| --------------------------- | ------- | -------------------- |
| `ENABLE_CONTROLLER_SUPPORT` | OFF     | 启用SDL2手柄支持（可能不一定提供了） |
| `ENABLE_DEBUG_OUTPUT`       | OFF     | 启用debug prints       |

Example with options:

```bash
cmake .. -DENABLE_CONTROLLER_SUPPORT=ON -DENABLE_DEBUG_OUTPUT=ON
```

---

### 安装（可选）

```bash
./build-package.sh
sudo dpkg -i speedynote.deb
```

---

### 疑难解答

#### 找不到MuPDF

**信息:** `⚠️ MuPDF not found - PDF export will be disabled`

**修复:** 安装MuPDF和其依赖

```bash
sudo apt install libmupdf-dev libharfbuzz-dev libfreetype-dev libjpeg-dev libopenjp2-7-dev libgumbo-dev libmujs-dev
```

#### 没有找到Qt6

**错误:** `Could not find a package configuration file provided by "Qt6"`

**修复:** 安装Qt6开发包

```bash
sudo apt install qt6-base-dev qt6-tools-dev
```

---

### 平台注释

#### ARM64 (树莓派, 运行Asahi Linux的Mac)

构建系统会自动识别ARM64，你不需要任何额外的操作就可以让它正常构建。

#### Fedora / 基于RHEL的系统

包名不太一样而已:

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel mupdf-devel harfbuzz-devel freetype-devel libjpeg-turbo-devel openjpeg2-devel gumbo-parser-devel mujs-devel
```

#### Arch Linux

```bash
sudo pacman -S cmake qt6-base qt6-tools mupdf harfbuzz freetype2 libjpeg-turbo openjpeg2 gumbo-parser mujs
```

#### Alpine Linux 和 postmarketOS

```bash
sudo apk add build-base cmake abuild qt6-qtbase-dev qt6-qttools-dev qt6-qtdeclarative-dev mupdf-dev
./compile.sh
./build-alpine-arm64.sh
```

---

### 另见：

- [Windows Build Guide](SpeedyNote_Windows_Build_en.md)
- [macOS Build Guide](SpeedyNote_Darwin_Build_en.md)
- [Android Build Guide](ANDROID_BUILD_GUIDE.md)
