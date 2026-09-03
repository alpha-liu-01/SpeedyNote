# SpeedyNote macOS 构建指南


### 准备工作

- 用于构建的 Mac 需运行 macOS 14+，ARM64 或 x86-64。构建出的应用可在 macOS 12+ 上运行。
- Xcode 命令行工具（`xcode-select --install`）。

---

### 环境配置

只有 Qt 的安装方式需要你自己选择，两种方式都可以：

- **Homebrew：** `brew install qt@6`
- **Qt 在线安装器 / aqtinstall：** 安装 macOS 版 Qt 6.9.3 或更高版本，然后通过
  `export SPEEDYNOTE_QT_PREFIX=~/Qt/6.9.3/macos` 让构建脚本指向它。CI 使用这种方式，
  因为 Homebrew 的 `qt@6` 会引入 umbrella `qt` formula（61 个依赖，包含 QtWebEngine 和
  Node.js），而且已经不再提供 Intel 架构的预编译包。

其余软件包：

```zsh
brew install cmake pkg-config librsvg
```

`librsvg` 提供 `rsvg-convert`，用于把 SVG 应用图标渲染成 `AppIcon.icns`。
缺少它会导致生成的 app 没有图标。

注意现在不再需要 `brew install mupdf`。MuPDF 由 `macos/build-mupdf.sh` 从源码编译为静态库，
`compile-mac.sh` 会在首次构建时自动调用它（约 10-20 分钟，之后可复用）。Homebrew 的 MuPDF
预编译包及其 dylib 依赖（freetype、harfbuzz、jpeg-turbo、openjpeg、jbig2dec、gumbo、brotli）
是针对打包时所用的 macOS 版本构建的，把它们打进 app 会把最低系统要求锁死在 macOS 14。
静态构建则统一使用 `-mmacosx-version-min=12.0` 编译这些依赖。

### 构建

运行 `compile-mac.sh` 来构建 SpeedyNote。编译产物位于 `build/` 目录，
app 包和 DMG 会生成在项目根目录。

```zsh
./compile-mac.sh            # 交互式
./compile-mac.sh -d --no-cli --strict   # CI 使用的参数：生成 DMG、跳过 CLI 询问、严格校验
```

`--strict` 会让打包校验从"提示"变成"失败退出"。任何准备分发的构建都应加上它：
否则如果 app 仍然引用 `/opt/homebrew`、`/usr/local` 或 `/Users`，脚本只会打印一条警告，
在你自己的机器上能正常启动，但在别人的机器上会失败。

`macdeployqt` 会打包 Qt 框架，因此最终的 app 不需要用户安装 Qt。

### 验证构建是否可分发

CI 工作流会自动检查以下几项；如果你改动了打包相关的逻辑，建议在本地也复查一遍：

```zsh
vtool -show-build SpeedyNote.app/Contents/MacOS/speedynote   # 期望：minos 12.0
otool -L SpeedyNote.app/Contents/MacOS/speedynote           # 期望：不含 /opt/homebrew、/usr/local、/Users
ls SpeedyNote.app/Contents/Resources/AppIcon.icns
```

`Contents/Frameworks/` 和 `Contents/PlugIns/` 下的每个 Mach-O 文件的 `minos`
也应当不高于 12.0。数值更高说明打包进了会抬高最低系统版本要求的文件。

## 已知问题

~~本文档可能无法反映 SpeedyNote 在基于 arm64 的 Mac 上的构建过程。GitHub 上提供的 dmg 文件仅适用于 x86-64 架构，因此可能无法在基于 ARM 的机器上运行。对于 Apple Silicon Mac 用户，我强烈建议您编译一个 arm64 原生二进制文件，步骤应该相似甚至相同。~~ SpeedyNote已经被确认可以在arm64的Mac上正常构建和运行。

构建结果是单架构而非通用二进制：arm64 Mac 生成 arm64 应用，Intel Mac 生成 x86_64 应用。
GitHub Releases 同时提供两者。
