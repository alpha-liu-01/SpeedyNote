# 📝 SpeedyNote

<div align="center">

<img src="https://i.imgur.com/tesbk4U.png" width="200" alt="SpeedyNote Logo">

**一款运行超级快，用起来超级高效的笔记应用，专为手写笔用户打造**

*专为需要在低成本硬件上获得远超 iPad 级别笔记体验的人群打造*

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20Android-brightgreen)]()
[![Qt](https://img.shields.io/badge/Qt-6.x-41CD52?logo=qt)]()

[![Get it on Flathub](https://flathub.org/assets/badges/flathub-badge-i-en.png)](https://flathub.org/zh-Hans/apps/org.speedynote.SpeedyNote)

[English](../../README.md) • [中文](.zh_Hans/README_zh_Hans.md)

</div>

---

## 为什么选择 SpeedyNote？

| 问题                                       | SpeedyNote 的解决方案                     |
| ---------------------------------------- | ------------------------------------ |
| 💸 OneNote 不支持 PDF 注释                    | ✅ 完整 PDF 支持，快速渲染                     |
| 🐌 Xournal++ 页数一多，速度像蜗牛爬                 | ✅ 在 赛扬 N4000 (1.1GHz) 上实现 360Hz 输入   |
| 💰 GoodNotes/Notability 每年收费好几百元且需要 iPad | ✅ 免费开源，在200元寨板上运行                    |
| 📱 大多数笔记应用仅限移动端或仅限桌面端                    | ✅ Windows、Linux、macOS 和 Android 统一体验 |

---

## ✨ 功能特性

### 🚀 性能优先

- **360Hz 手写笔轮询** 在低端硬件上（测试于 Celeron N4000 @ 1.1GHz）
- **即时 PDF 加载** - 大型文档可在数秒内打开，完全不需要数分钟
- **小内存占用** - 原生 C++ 编写，绝对不是套壳浏览器
- **ARM64 原生构建** - 针对骁龙笔记本和瑞芯微Chromebook 优化

### 🖊️ 专业绘图工具

- **压感墨水** 支持笔、记号笔和文字标画工具
- **矢量笔画** - 不论缩放比例永远抗锯齿
- **多图层编辑**（SAI2 风格）- 添加、删除、重新排序、合并图层
- **笔画橡皮擦** 支持完整撤销/重做
- **触控手势** - 双指平移、捏合缩放、手掌防误触

### 📄 文档模式

- **分页笔记本** - 传统逐页笔记（`.snb`）
- **无边画布** - 无限白板，支持懒加载图块（`.snb`）
- **PDF 背景** - 在 PDF 上注释，支持可点击内部链接
- **分享** - `.snbx` 笔记包支持跨平台笔记分享

### 🎯 平板优先用户体验

- **操作栏** - 需要时显示上下文相关按钮
- **子工具栏** - 无需深入菜单即可快速访问工具设置
- **页面面板** - 缩略图导航，支持拖拽重新排序
- **PDF 大纲** - 点击目录条目跳转到章节

### 🔗 高级功能

- **链接对象** - 创建可点击链接指向 Markdown 笔记、URL 或位置
- **Markdown 笔记** - 在任何页面或位置附加富文本笔记
- **多标签页编辑** - 同时处理多个文档

---

## 📸 截图

<!-- TODO: Replace with actual screenshots -->

| PDF 注释                                  | 图层面板                                       | 页面缩略图                                     |
| --------------------------------------- | ------------------------------------------ | ----------------------------------------- |
| ![PDF](https://i.imgur.com/xgmYhfK.png) | ![Layers](https://i.imgur.com/NelpAMv.png) | ![Pages](https://i.imgur.com/A93UeAT.png) |

| 无边画布                                         | 操作栏                                        | 子工具栏                                           |
| -------------------------------------------- | ------------------------------------------ | ---------------------------------------------- |
| ![Edgeless](https://i.imgur.com/wHLeyIj.png) | ![Action](https://i.imgur.com/wHLeyIj.png) | ![Subtoolbar](https://i.imgur.com/VSvZaxA.png) |

| 链接对象                                            | Markdown 支持                                  | Android                                     |
| ----------------------------------------------- | -------------------------------------------- | ------------------------------------------- |
| ![LinkObjects](https://i.imgur.com/QkEw57Y.png) | ![Markdown](https://i.imgur.com/yKVJw5E.png) | ![Android](https://i.imgur.com/rfAJMNF.png) |

---

## 🚀 入门指南

### 系统要求

| 平台          | 最低要求                     | 推荐配置             |
| ----------- | ------------------------ | ---------------- |
| **Windows** | Windows 10 1809          | Windows 11       |
| **macOS**   | macOS 13                | macOS 15+        |
| **Linux**   | Ubuntu 22.04 / Fedora 38 | 任何支持 Qt 6.4+ 的系统 |
| **Android** | Android 9 (API 28)     | Android 13+      |

**硬件：** 任何 x86_64 或 ARM64 CPU。已在 Intel Core i5 470UM（2010）、赛扬 N4000、高通骁龙 7c Gen 2、瑞芯微 RK3399 上测试

### 💾 安装

#### Windows / macOS / Linux

从 **[GitHub Releases](https://github.com/alpha-liu-01/SpeedyNote/releases)** 或官方网站下载最新版本。

| 平台            | 包格式               |
| ------------- | ----------------- |
| Windows       | `.exe` 安装程序       |
| macOS         | `.dmg` 磁盘映像       |
| Debian/Ubuntu | `.deb` 包          |
| Fedora/RHEL   | `.rpm` 包          |
| Arch Linux    |  AUR `yay -S speedynote`  |

#### Android

**选项 1：Google Play Store**（即将推出），支持开发  
**选项 2：从源码构建** - 免费，详见 [Android 构建指南](../build_docs/ANDROID_BUILD_GUIDE.md)

> Play Store 版本为便利性收费。源码始终在 GPL v3 下免费。

#### Linux APT 仓库（Debian/Ubuntu）

```bash
# 添加仓库并安装
wget -O- https://apt.speedynote.org/speedynote-release-key.gpg | \
  sudo gpg --dearmor -o /etc/apt/trusted.gpg.d/speedynote.gpg

echo "deb [arch=amd64,arm64 signed-by=/etc/apt/trusted.gpg.d/speedynote.gpg] \
  https://apt.speedynote.org stable main" | \
  sudo tee /etc/apt/sources.list.d/speedynote.list

sudo apt update && sudo apt install speedynote
```

---

## 🛠️ 从源码构建

### 先决条件

| 平台      | 要求                                                              |
| ------- | --------------------------------------------------------------- |
| 全部      | CMake 3.16+、C++17 编译器                                           |
| Windows | MSYS2 及 clang64/clangarm64 工具链                                  |
| macOS   | Xcode 命令行工具、Homebrew                                            |
| Linux   | Qt 6.4+ 开发包、Poppler-Qt6                                         |
| Android | Docker（详见 [Android 构建指南](../build_docs/ANDROID_BUILD_GUIDE.md)） |

### 快速构建

```bash
# 克隆仓库
git clone https://github.com/alpha-liu-01/SpeedyNote.git
cd SpeedyNote

# Windows (MSYS2 clang64 shell)
./compile.ps1

# macOS
./compile-mac.sh

# Linux
./compile.sh
# 或构建包：./build-package.sh
```

### 详细构建指南

- [Windows 构建指南](./docs/build_docs/SpeedyNote_Windows_Build_en.md)
- [macOS 构建指南](./docs/build_docs/SpeedyNote_Darwin_Build_en.md)  # 暂时无效
- [Android 构建指南](./docs/build_docs/ANDROID_BUILD_GUIDE.md)

---

## 📁 文件格式

| 格式      | 描述        | 使用场景      |
| ------- | --------- | --------- |
| `.snb`  | 包含图块的包文件夹 | 无边画布、大型项目 |
| `.snbx` | 压缩包（ZIP）  | 分享、备份     |

**注意：** 不支持 v0.x 的遗留 `.spn` 格式。

---

## 🌍 支持语言

SpeedyNote 支持多种语言：

- 🇺🇸 英语
- 🇨🇳 简体中文
- 🇪🇸 西班牙语（部分）
- 🇫🇷 法语（部分）

> 欢迎为更多翻译做出贡献！

---

## 🤝 贡献

欢迎贡献！请随时：

- 🐛 通过 [GitHub Issues](https://github.com/alpha-liu-01/SpeedyNote/issues) 报告错误
- 💡 建议功能
- 🌍 添加翻译
- 🔧 提交拉取请求

---

## 📜 许可证

SpeedyNote 采用 **GNU General Public License v3.0** 许可证。

- ✅ 免费使用、修改和分发
- ✅ 源码始终可用
- ✅ 允许商业使用（Play Store 版本）
- 📋 衍生作品必须也采用 GPL v3

详见 [LICENSE](../LICENSE)。

### 第三方库

| 库                 | 许可证     | 使用用途         |
| ----------------- | ------- | ------------ |
| Qt 6              | LGPL v3 | UI 框架        |
| MuPDF             | AGPL v3 | PDF 渲染、导出    |
| QMarkdownTextEdit | MIT     | Markdown 编辑器 |
| miniz             | MIT     | ZIP 压缩       |

---

## 💖 支持项目

如果 SpeedyNote 对您有帮助，请考虑：

- ☕ [请我喝杯咖啡](https://buymeacoffee.com/alphaliu01)
- ⭐ 为此仓库加星
- 📱 在 Google Play 上购买 Android 版本
- 🐛 报告错误并建议改进
- 🌍 贡献翻译

---

<div align="center">

**用 ❤️ 为值得更好工具的学生打造**

*SpeedyNote v1.0*

</div>