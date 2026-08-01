# SpeedyNote macOS 构建指南


### 准备工作

- Macintosh (macOS 13+(?)), ARM64 或 x86-64

---

### 环境配置

##### Qt 在线安装器

使用默认设置进行安装。

安装以下软件包：

```zsh
brew install qt@6
brew install poppler

```
##### 额外库

由于 `poppler-qt6` 在 `brew` 中不可用，我自己编译了它，您可以在我们的 QQ 群或网站 `speedynote.org` 的社区页面找到。将这个 `poppler-qt6` 文件夹放入 `/opt/` 目录中。或者您可以运行 `build_poppler_qt6.sh` 脚本从源代码构建 poppler-qt6，它会自动放置在 /opt/ 目录下。

### 构建

运行 `compile-mac.sh` 来构建 SpeedyNote。完整的构建结果在 `build` 目录中。构建 .app 脚本的选项 3 可能无法正常工作，因此用户仍然需要 `brew install qt@6` 来使应用程序运行。生成的 `dmg` 文件包含一个脚本，用于让新用户设置依赖项。

## 已知问题

本文档可能无法反映 SpeedyNote 在基于 arm64 的 Mac 上的构建过程。GitHub 上提供的 dmg 文件仅适用于 x86-64 架构，因此可能无法在基于 ARM 的机器上运行。对于 Apple Silicon Mac 用户，我强烈建议您编译一个 arm64 原生二进制文件，步骤应该相似甚至相同。


