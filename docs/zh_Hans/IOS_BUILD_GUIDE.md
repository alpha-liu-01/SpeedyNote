# SpeedyNote iPadOS 构建指南

**文档版本：** 1.0  
**日期：** 2026 年 2 月  
**状态：** 已验证可用

---

## 概述

本指南提供在 iPadOS 上构建 SpeedyNote 的逐步说明。构建系统支持三类目标：iOS 模拟器（用于开发）、已配置的设备构建（用于在未越狱 iPad 上测试）以及临时/离线分发的设备构建（用于通过 `.deb` 在越狱设备上分发，或通过 TrollStore 使用 `.ipa` 分发）。

### 架构

- **目标：** iPadOS arm64（设备），x86_64（Intel 模拟器）  
- **PDF 后端：** MuPDF 1.24.10（交叉编译，静态链接）  
- **UI 框架：** Qt 6.9.3 for iOS（静态链接）  
- **最低部署：** iPadOS 16.0

所有依赖项（Qt、MuPDF、Noto 字体）都被静态链接进单个二进制文件。除了 Apple 系统框架外，不依赖其它动态库。

---

## 前置条件

### 主机系统要求

- macOS 13+（Ventura 或更高）  
- Xcode 15+（包含 iOS 模拟器运行时和命令行工具）  
- Homebrew（`brew`）  
- 约 15 GB 可用磁盘空间（用于 Qt、MuPDF 源代码和构建产物）

### 软件依赖

| 依赖 | 安装方式 | 用途 |
|------|---------|------|
| Qt 6.9.3 for iOS | Qt 在线安装器 或 `aqtinstall` | 所有构建 |
| Xcode CLI 工具 | `xcode-select --install` | 所有构建 |
| librsvg | `brew install librsvg` | 图标生成 |
| ldid | `brew install ldid` | 仅用于 ad-hoc 设备构建 |
| dpkg | `brew install dpkg` | 仅用于 `.deb` 打包 |

### Qt 安装

必须将 Qt 6.9.3 for iOS 安装到 `~/Qt/6.9.3/ios/`。构建脚本期望 `qt-cmake` 位于 `~/Qt/6.9.3/ios/bin/qt-cmake`。

通过 Qt 在线安装器安装（选择 Qt 6.9.3 下的 “iOS” 组件），或使用 `aqtinstall`：

```bash
pip install aqtinstall
aqt install-qt mac ios 6.9.3
```

---

## 快速开始

### 模拟器（开发）

```bash
# 为模拟器交叉编译 MuPDF（首次运行）
./ios/build-mupdf.sh --simulator

# 生成应用图标（首次运行）
./ios/generate-icons.sh

# 在模拟器中构建并运行
./ios/build-sim.sh
./ios/run-sim.sh
```

### 设备 — 已配置（在真实 iPad 上测试）

```bash
# 为设备交叉编译 MuPDF（首次运行）
./ios/build-mupdf.sh

# 构建、安装并运行
./ios/build-device.sh YOUR_TEAM_ID
./ios/run-device.sh
```

### 设备 — 临时/离线（越狱 / TrollStore 分发）

```bash
# 为设备交叉编译 MuPDF（首次运行）
./ios/build-mupdf.sh

# 以 Release 构建并用 ldid 伪签名
./ios/build-device.sh

# 打包以分发
./ios/build-deb.sh   # 生成 rootless .deb（用于 Sileo）
./ios/build-ipa.sh   # 生成 .ipa（用于 TrollStore）
```

---

## 详细构建说明

### 第 1 阶段：生成应用图标

图标由 `resources/icons/mainicon.svg` 渲染为 1024x1024 的 PNG，用于 Xcode 的 Asset Catalog。现代 iOS 会自动生成其它尺寸。

```bash
./ios/generate-icons.sh
```

需要 `rsvg-convert`（`brew install librsvg`）。

输出：`ios/Assets.xcassets/AppIcon.appiconset/icon-1024.png`

此步骤只需在首次或 SVG 图标变更时执行。

---

### 第 2 阶段：交叉编译 MuPDF

MuPDF 提供 PDF 渲染与导出功能。必须将其作为静态库交叉编译为目标平台架构。

构建脚本会自动下载 MuPDF 1.24.10 源码，移除捆绑的 HarfBuzz（以避免与 Qt 捆绑的 HarfBuzz 符号冲突），并进行编译。

#### 针对模拟器

```bash
./ios/build-mupdf.sh --simulator
```

输出：
- `ios/mupdf-build-sim/lib/libmupdf.a`
- `ios/mupdf-build-sim/lib/libmupdf-third.a`
- `ios/mupdf-build-sim/include/mupdf/*.h`

#### 针对设备

```bash
./ios/build-mupdf.sh
```

输出：
- `ios/mupdf-build/lib/libmupdf.a`
- `ios/mupdf-build/lib/libmupdf-third.a`
- `ios/mupdf-build/include/mupdf/*.h`

注意：模拟器与设备构建使用不同架构（x86_64 vs arm64）和不同 SDK。若需两者，请分别带或不带 `--simulator` 运行脚本。源码压缩包只会下载一次并共享。

---

### 第 3 阶段：构建 SpeedyNote

#### 3A. 模拟器构建

模拟器构建是最快的迭代方式。无需 Apple ID 或签名。

```bash
./ios/build-sim.sh
```

| 标志 | 说明 |
|------|------|
| `--clean` | 在配置前删除构建目录 |
| `--rebuild` | 跳过 CMake 配置步骤，仅重新编译 |
| `-h`, `--help` | 显示使用信息 |

构建使用 Xcode 生成器（`-GXcode`）并以 `iphonesimulator` SDK 为目标。产物为 Debug `.app` 包：`ios/build-sim/Debug-iphonesimulator/speedynote.app`。

#### 3B. 设备构建 — 已配置（Provisioned）

用于在你可接触的未越狱 iPad 上测试。需要免费或付费的 Apple 开发者账号。

```bash
./ios/build-device.sh YOUR_TEAM_ID
```

**查找 Team ID：**  
Xcode → Settings → Accounts → 选择你的 Apple ID → 10 字符的字母数字字符串即为 Team ID。

| 标志 | 说明 |
|------|------|
| `TEAM_ID` | Apple 开发者 Team ID（第一个位置参数） |
| `--clean` | 在配置前删除构建目录 |
| `--rebuild` | 跳过 CMake 配置步骤，仅重新编译 |
| `--release` | 构建 Release 而非 Debug |
| `-h`, `--help` | 显示使用信息 |

如果不使用 `--release`，provisioned 构建默认是 Debug（便于在 Xcode 中调试）。输出路径：`ios/build-device/Debug-iphoneos/speedynote.app`（使用 `--release` 时为 `Release-iphoneos/`）。

**设备首次设置：**
1. 在 iPad 上启用开发者模式：设置 → 隐私与安全 → 开发者模式  
2. 使用 USB-C 连接 iPad 并信任此电脑  
3. 首次安装后，在 iPad 上信任开发者证书：设置 → 通用 → VPN 与设备管理 → 点击你的配置文件 → 信任

#### 3C. 设备构建 — 临时/离线（Ad-hoc）

用于在越狱 iPad 或通过 TrollStore 分发。无需 Apple ID。

```bash
./ios/build-device.sh
```

当未提供 `TEAM_ID` 时，脚本将：
1. 以 Release 模式构建（始终如此）  
2. 向 Xcode 传递 `CODE_SIGN_IDENTITY=-` 与 `CODE_SIGNING_ALLOWED=NO`  
3. 使用 `ldid -S` 对二进制进行伪签名  
4. 使用 `strip -x` 去除调试符号

需要 `ldid`（`brew install ldid`）。

输出：`ios/build-device/Release-iphoneos/speedynote.app`

---

### 第 4 阶段：运行

#### 模拟器

```bash
./ios/run-sim.sh
```

| 标志 | 说明 |
|------|------|
| `--list` | 列出可用的 iPad 模拟器 |
| `DEVICE_NAME` | 指定模拟器名称（例如 `"iPad (A16)"`） |
| `-h`, `--help` | 显示使用信息 |

若未指定设备名称，将自动选择第一个可用的 iPad 模拟器。脚本会启动模拟器、打开 Simulator.app、安装 `.app` 包并启动应用。

注意：Apple Pencil 输入无法模拟。使用鼠标/触控板进行基本界面操作。

#### 设备（已配置）

```bash
./ios/run-device.sh
```

| 标志 | 说明 |
|------|------|
| `--list` | 列出已连接的 iOS 设备 |
| `--ipa` | 同时创建一个 `.ipa` 包 |
| `-h`, `--help` | 显示使用信息 |

另一个选项是在 Xcode 中打开工程并按 Cmd+R 部署：

```bash
open ios/build-device/SpeedyNote.xcodeproj
```

---

### 第 5 阶段：为分发打包

打包仅在 ad-hoc 设备构建（未提供 `TEAM_ID`）场景下相关。

#### `.deb` — 适用于越狱 iPad（Sileo）

```bash
./ios/build-deb.sh
```

需要 `dpkg-deb`（`brew install dpkg`）。

该脚本创建一个 rootless `.deb` 包，安装路径为 `/var/jb/Applications/SpeedyNote.app/`。包内包含 `postinst` 与 `postrm` 脚本，用于运行 `uicache` 注册/注销应用图标。

输出：`ios/dist/SpeedyNote_<version>_iphoneos-arm64.deb`

**在越狱设备上安装：**

```bash
scp ios/dist/SpeedyNote_1.2.5_iphoneos-arm64.deb root@<ipad-ip>:/tmp/
ssh root@<ipad-ip> 'dpkg -i /tmp/SpeedyNote_1.2.5_iphoneos-arm64.deb'
```

或将 `.deb` 放到 APT 仓库以供 Sileo 安装。

#### `.ipa` — 适用于 TrollStore

```bash
./ios/build-ipa.sh
```

生成标准 `.ipa`（压缩的 `Payload/SpeedyNote.app/` 结构）。

输出：`ios/dist/SpeedyNote_<version>.ipa`

通过 AirDrop 或 USB 将 `.ipa` 传到 iPad，然后使用 TrollStore 打开安装。

---

## 构建脚本参考

所有脚本位于 `ios/` 目录，且应从 SpeedyNote 项目根目录运行。

| 脚本 | 说明 |
|------|------|
| `ios/generate-icons.sh` | 将 SVG 渲染为 1024x1024 PNG 用于 Asset Catalog |
| `ios/build-mupdf.sh [--simulator]` | 交叉编译 MuPDF（默认设备，或 `--simulator`） |
| `ios/build-sim.sh [--clean] [--rebuild]` | 为 iOS 模拟器配置并构建 |
| `ios/build-device.sh [TEAM_ID] [--clean] [--rebuild] [--release]` | 为设备配置并构建 |
| `ios/run-sim.sh [--list] [DEVICE_NAME]` | 在模拟器中安装并启动 |
| `ios/run-device.sh [--list] [--ipa]` | 在已连接设备上安装并启动 |
| `ios/build-deb.sh` | 将 ad-hoc 构建打包为 rootless `.deb` |
| `ios/build-ipa.sh` | 将 ad-hoc 构建打包为 `.ipa` |

---

## 目录结构

```
SpeedyNote/
├── ios/
│   ├── Info.plist                  # iOS 应用元数据
│   ├── LaunchScreen.storyboard     # 启动画面
│   ├── Assets.xcassets/            # 资源目录
│   │   └── AppIcon.appiconset/
│   │       ├── Contents.json
│   │       └── icon-1024.png       # 由 generate-icons.sh 生成
│   │
│   ├── generate-icons.sh           # 图标生成脚本
│   ├── build-mupdf.sh             # MuPDF 交叉编译脚本
│   ├── build-sim.sh               # 模拟器构建脚本
│   ├── build-device.sh            # 设备构建脚本（provisioned 或 ad-hoc）
│   ├── run-sim.sh                 # 模拟器运行脚本
│   ├── run-device.sh              # 设备安装/启动脚本
│   ├── build-deb.sh               # .deb 打包脚本
│   ├── build-ipa.sh               # .ipa 打包脚本
│   │
│   ├── mupdf-src/                 # MuPDF 源码（下载后被 gitignore）
│   ├── mupdf-build/               # MuPDF 设备库（生成）
│   ├── mupdf-build-sim/           # MuPDF 模拟器库（生成）
│   ├── build-sim/                 # 模拟器 CMake/Xcode 构建产物（生成）
│   ├── build-device/              # 设备 CMake/Xcode 构建产物（生成）
│   └── dist/                      # 打包产物（生成）
│
├── source/
│   └── ios/
│       ├── IOSPlatformHelper.h/mm  # 深色模式、字体、手势修复
│       ├── IOSShareHelper.h/mm     # 分享表单（导出）
│       ├── PdfPickerIOS.h/mm       # 原生 PDF 文件选择器
│       └── SnbxPickerIOS.h/mm      # 原生 SNBX 文件选择器
│
├── resources/icons/
│   └── mainicon.svg                # 源图标
|
└── CMakeLists.txt                  # 构建配置（iOS 部分）
```

---

## 平台相关说明

### 静态链接

Qt for iOS 始终以静态方式链接（符合 Apple 对第三方框架的政策）。这意味着：
- 整个 Qt 运行时被编译进单个 `speedynote` 二进制文件  
- 不需要在 `.app` 包中携带 `.framework` 或 `.dylib` 文件  
- 二进制只链接 Apple 系统框架（UIKit、CoreGraphics、Metal 等）  
- MuPDF 及其内置的 Noto 字体也以静态方式链接

### HarfBuzz 冲突

MuPDF 捆绑了自己的 HarfBuzz，并使用自定义分配器，要求传入 `fz_context*`。而 Qt 也会捆绑 HarfBuzz。当两者同时静态链接时，符号会冲突，导致 Qt 调用到 MuPDF 的 HarfBuzz（但没有必要的上下文），在 `fz_calloc_no_throw` 中崩溃。

`build-mupdf.sh` 脚本通过从构建中剥离 MuPDF 捆绑的 HarfBuzz 源文件来解决该问题。SpeedyNote 仅使用 MuPDF 做 PDF 渲染/导出，并不调用 MuPDF 的 HTML/EPUB 布局引擎（后者是 HarfBuzz 的主要使用方）。

### 代码签名模式

| 模式 | 签名方式 | 使用场景 |
|------|---------|---------|
| 模拟器 | 无需签名 | 开发 |
| Provisioned（已配置） | Apple Development（自动签名） | 在真实 iPad 上测试 |
| Ad-hoc（临时/离线） | `ldid -S`（伪签名） | 越狱 iPad、TrollStore |

### 条件编译

iOS 特定代码用 `Q_OS_IOS` 进行保护：

```cpp
#ifdef Q_OS_IOS
#include "ios/IOSPlatformHelper.h"
#include "ios/IOSShareHelper.h"
#endif
```

在 iOS 上禁用的功能：
- `QLocalServer` 单实例锁（iOS 沙箱不支持）  
- `QProcess::startDetached`（在 iOS 上不可用）  
- SDL2 手柄支持  
- SpeedyNote CLI（iOS 上无终端）

---

## 故障排查

### 找不到 Qt

**错误：** `Qt 6.9.3 for iOS not found at ~/Qt/6.9.3/ios/bin/qt-cmake`

**解决：** 使用 Qt 在线安装器或 `aqtinstall` 安装包含 iOS 组件的 Qt 6.9.3。

### 找不到 MuPDF

**错误：** `MuPDF (device) not found at ios/mupdf-build/lib/libmupdf.a`

**解决：** 运行 MuPDF 交叉编译脚本：
```bash
./ios/build-mupdf.sh              # 设备
./ios/build-mupdf.sh --simulator  # 模拟器
```

### 模拟器： "No iPad Simulator found"

**解决：** 在 Xcode 中安装一个 iOS Simulator 运行时：Xcode → Settings → Platforms → 下载相应 iOS 运行时。

列出可用模拟器：
```bash
./ios/run-sim.sh --list
```

### 设备： "ldid not found"

**解决：** 安装 ldid（仅用于 ad-hoc 构建）：
```bash
brew install ldid
```

### 设备：配置失败（provisioning failure）

**错误：** Xcode 签名或配置描述文件错误

**解决：**
1. 验证 Team ID：Xcode → Settings → Accounts → 你的 Apple ID  
2. 确保 iPad 上已启用开发者模式  
3. 在 iPad 上信任开发者证书：设置 → 通用 → VPN 与设备管理

### 架构不匹配

**错误：** 链接器关于错误架构的警告（例如：在 arm64 设备构建中错误地链接了 x86_64 的 MuPDF）

**解决：** MuPDF 必须分别为模拟器和设备编译：
- `ios/mupdf-build/` — 设备（arm64）  
- `ios/mupdf-build-sim/` — 模拟器（x86_64）

在切换目标后可使用 `--clean` 强制完全重新配置：
```bash
./ios/build-device.sh --clean
```

---

## 清理构建

要进行完全清理构建：

```bash
# 删除所有生成的构建产物
rm -rf ios/build-sim ios/build-device ios/dist

# （可选）删除 MuPDF 构建目录（需要重新编译）
rm -rf ios/mupdf-build ios/mupdf-build-sim

# 从头重建
./ios/build-mupdf.sh --simulator   # 如果目标是模拟器
./ios/build-mupdf.sh               # 如果目标是设备
./ios/build-sim.sh --clean         # 或 ./ios/build-device.sh --clean
```

---

## 参见

- [IOS_BUG_TRACKING.md](../IOS_BUG_TRACKING.md) — 已知问题与修复  
- [ANDROID_BUILD_GUIDE.md](ANDROID_BUILD_GUIDE.md) — Android 构建指南（架构类似）  
- [MuPDF Documentation](https://mupdf.com/docs/)  
- [Qt for iOS](https://doc.qt.io/qt-6/ios.html)

