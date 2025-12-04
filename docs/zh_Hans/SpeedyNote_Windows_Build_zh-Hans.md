# SpeedyNote Windows 构建

### 您需要准备的东西

- Windows 10 1809+ 或者 Windows 11 (x86_64) / Windows 11 (arm64)

- MSYS2

- InnoSetup

---

### 准备环境

##### Qt Online Installer

你已经不需要使用Qt Online Installer了。

##### MSYS2

下载安装就可以了，安装在默认位置无需变化。  

安装这样几个软件包

```bash
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf
pacman -S mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-qt6-tools mingw-w64-clang-x86_64-qt6-declarative mingw-w64-clang-x86_64-qt6-translations
pacman -S mingw-w64-clang-x86_64-poppler mingw-w64-clang-x86_64-poppler-qt6
pacman -S mingw-w64-clang-x86_64-SDL2
```

**对于 ARM64 设备：** 将上述命令中的 `x86_64` 替换为 `aarch64`，并在下方路径中使用 `clangarm64` 代替 `clang64`。 

##### Path

下列几个路径需要添加到Path中。

```cmd
C:\msys64\clang64\bin
```
arm64机器的改动同理。

然后重启计算机

---

### 编译

运行 `compile.ps1`即可完成编译。编译后的内容在`build`文件夹中。你可以选择将一些CMake产生的临时文件删去来减小磁盘占用。对于arm64的设备来说，应该运行`compile.ps1 -arm64` 。

---

### 打包

使用`InnoSetup`进行打包。您只需要按播放按钮，它就能自动将`build`文件夹中的一切打包为安装包，并修改注册表来添加打开`PDF` 和 `spn`的选项。
