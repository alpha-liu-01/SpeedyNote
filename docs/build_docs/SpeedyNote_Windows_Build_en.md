# SpeedyNote Windows Build

### Preparation

- Windows 10 1809+ x86-64, Windows 11 x86-64 or ARM64

- Official MSYS2 installer

- InnoSetup

---

### Environment

##### Qt Online Installer

Qt online installer is **no longer needed**.

##### MSYS2

Install with default settings.  

Install these packages:

```bash
pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-pkgconf
pacman -S mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-qt6-tools mingw-w64-clang-x86_64-qt6-declarative mingw-w64-clang-x86_64-qt6-translations
pacman -S mingw-w64-clang-x86_64-poppler mingw-w64-clang-x86_64-poppler-qt6
pacman -S mingw-w64-clang-x86_64-SDL2
```
``mingw-w64-clang-x86_64-pkgconf`` is only used for ``lupdate-qt6.exe`` to parse the new translations.

**For ARM64 systems:** Replace `x86_64` with `aarch64` in the commands above, and use `clangarm64` instead of `clang64` in the paths below. 

##### Path

Add this directory to Path. If you have an arm64 system, replace the paths from clang64 to clangarm64. 

```cmd
C:\msys64\clang64\bin
```

and then restart your PC.

##### CMake Setup

In the CMake Tools extension of Visual Studio Code, find the `Cmake: Cmake Path` option and change it to `C:\msys64\clang64\bin\cmake.exe` .
Add this directory to Path. As always, if you have an arm64 system, replace the paths from clang64 to clangarm64. 

---

### Build

Run `compile.ps1`to build SpeedyNote. The complete build is in `build`.You may want to delete some temporary files that CMake generated. For arm64 builds, you need to run `compile.ps1 -arm64` instead. 

---

### Packaging

Use`InnoSetup`to pack SpeedyNote. Hit the play button and InnoSetup automatically finish the packaging process. 
