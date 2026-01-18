# SpeedyNote macOS Build


### Preparation

- Macintosh (macOS 13+(?)), ARM64 or x86-64

---

### Environment

##### Qt Online Installer

Install with default settings.

Install these packages:

```zsh
brew install qt@6
brew install poppler
brew install sdl2

```
##### Additional libraries

Since `poppler-qt6` is not available on `brew`, I compiled it myself and you may find it in our QQ group or on the community page of our site `speedynote.org`. Put this `poppler-qt6`  folder into `/opt/`. Or you may run the `build_poppler_qt6.sh` script to build poppler-qt6 from source, and it will be positioned in /opt/ automatically. 

### Build

Run `compile-mac.sh`to build SpeedyNote. The complete build is in `build`. Option 3 for building the .app script may not work, so the user still needs to `brew install qt@6` to make the app run. The generated `dmg` contains an script to let new users set up the dependencies.

## Unknown issues

This build document may not reflect the building process of SpeedyNote on an arm64-based Mac. The dmg file offered on GitHub is x86-64 only, so it may not work on arm-based machines. For Apple Silicon Mac users, I highly recommend you compile an arm64 native binary, and the steps should be similar if not identical.


